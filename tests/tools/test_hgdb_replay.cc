#include <filesystem>

#include "../../tools/hgdb-replay/engine.hh"
#include "gtest/gtest.h"
#include "thread.hh"
#include "vpi_user.h"

void change_cwd() {
    namespace fs = std::filesystem;
    fs::path filename = __FILE__;
    auto dirname = filename.parent_path() / "vectors";
    fs::current_path(dirname);
}

TEST(vcd, vcd_parse) {  // NOLINT
    change_cwd();

    hgdb::vcd::VCDDatabase db("waveform1.vcd");

    // module resolution
    auto module_id = db.get_instance_id("top");
    EXPECT_TRUE(module_id);
    EXPECT_EQ(*module_id, 0);
    module_id = db.get_instance_id("top.inst");
    EXPECT_TRUE(module_id);
    EXPECT_EQ(*module_id, 1);
    // invalid module name
    module_id = db.get_instance_id("top2");
    EXPECT_FALSE(module_id);
    module_id = db.get_instance_id("top.inst2");
    EXPECT_FALSE(module_id);

    // signal resolution
    auto signal_id = db.get_signal_id("top.clk");
    EXPECT_TRUE(signal_id);
    signal_id = db.get_signal_id("top.inst.b");
    EXPECT_TRUE(signal_id);
    // array
    signal_id = db.get_signal_id("top.result[0]");
    EXPECT_TRUE(signal_id);
    // invalid signal names
    signal_id = db.get_signal_id("clk");
    EXPECT_FALSE(signal_id);
    signal_id = db.get_signal_id("top.inst.c");
    EXPECT_FALSE(signal_id);

    // query signal names
    auto signals = db.get_instance_signals(*db.get_instance_id("top"));
    // result -> 10, a, b, clk, num_cycles
    EXPECT_EQ(signals.size(), 10 + 4);
    signals = db.get_instance_signals(*db.get_instance_id("top.inst"));
    // a, clk, b
    EXPECT_EQ(signals.size(), 3);
    // invalid module handles
    signals = db.get_instance_signals(3);
    EXPECT_TRUE(signals.empty());

    // child instances
    auto instances = db.get_child_instances(*db.get_instance_id("top"));
    EXPECT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0].name, "inst");
    instances = db.get_child_instances(*db.get_instance_id("top.inst"));
    EXPECT_TRUE(instances.empty());
    // illegal query
    instances = db.get_child_instances(42);
    EXPECT_TRUE(instances.empty());

    auto signal = db.get_signal(*db.get_signal_id("top.a"));
    EXPECT_TRUE(signal);
    EXPECT_EQ(signal->name, "a");

    auto module = db.get_instance(0);
    EXPECT_TRUE(module);
    EXPECT_EQ(module->name, "top");

    auto value = db.get_signal_value(*db.get_signal_id("top.inst.b"), 20);
    EXPECT_EQ(*value, "1");
    value = db.get_signal_value(*db.get_signal_id("top.inst.b"), 40);
    EXPECT_EQ(*value, "10");
    value = db.get_signal_value(*db.get_signal_id("top.result[2]"), 40);
    EXPECT_EQ(*value, "x");
    value = db.get_signal_value(*db.get_signal_id("top.result[2]"), 61);
    EXPECT_EQ(*value, "1");
}

int cycle_count(p_cb_data cb_data) {
    auto *user_data = cb_data->user_data;
    auto *int_value = reinterpret_cast<int *>(user_data);
    (*int_value)++;
    return 0;
}

TEST(replay, clk_callback_waveform1) {  // NOLINT
    change_cwd();

    auto db = std::make_unique<hgdb::vcd::VCDDatabase>("waveform1.vcd");
    auto vpi = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));

    // need to add some callbacks before hand it over to the engine
    constexpr auto clk_name = "top.clk";
    auto *clk = vpi->vpi_handle_by_name(const_cast<char *>(clk_name), nullptr);
    EXPECT_NE(clk, nullptr);
    int cycle_count_int = 0;
    s_cb_data cb{.reason = cbValueChange,
                 .cb_rtn = cycle_count,
                 .obj = clk,
                 .user_data = reinterpret_cast<char *>(&cycle_count_int)};

    hgdb::replay::EmulationEngine engine(vpi.get());

    // register the CB
    auto *r = vpi->vpi_register_cb(&cb);
    EXPECT_NE(r, nullptr);

    engine.run();
    EXPECT_EQ(cycle_count_int, 10 * 2);
}

struct rewind_info {
    std::unordered_set<uint64_t> *values;
    bool *has_rewound;
    hgdb::AVPIProvider *vpi;
    vpiHandle clk;
};

int test_rewind_value_get(p_cb_data cb_data) {
    auto *user_data = cb_data->user_data;
    auto *info = reinterpret_cast<rewind_info *>(user_data);

    // first block on
    if (!(*info->has_rewound)) {
        // rewind the time!
        hgdb::AVPIProvider::rewind_data rewind_data;
        rewind_data.time = 100;
        rewind_data.clock_signals = {info->clk};
        info->vpi->vpi_rewind(&rewind_data);
        *info->has_rewound = true;
    } else {
        // get the times
        s_vpi_time current_time{};
        current_time.type = vpiSimTime;
        info->vpi->vpi_get_time(nullptr, &current_time);
        auto time = current_time.low;
        info->values->emplace(time);
    }

    return 0;
}

TEST(replay, get_value_reverse) {  // NOLINT
    change_cwd();

    auto db = std::make_unique<hgdb::vcd::VCDDatabase>("waveform1.vcd");
    auto vpi = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));
    hgdb::replay::EmulationEngine engine(vpi.get());

    // set the time to 10
    constexpr auto clk_name = "top.clk";
    auto *clk = vpi->vpi_handle_by_name(const_cast<char *>(clk_name), nullptr);
    EXPECT_NE(clk, nullptr);

    std::unordered_set<uint64_t> values;
    bool has_rewound = false;

    rewind_info cb_info{
        .values = &values, .has_rewound = &has_rewound, .vpi = vpi.get(), .clk = clk};

    s_cb_data cb{.reason = cbValueChange,
                 .cb_rtn = test_rewind_value_get,
                 .obj = clk,
                 .user_data = reinterpret_cast<char *>(&cb_info)};

    // register the CB
    auto *r = vpi->vpi_register_cb(&cb);
    EXPECT_NE(r, nullptr);

    engine.run(false);

    engine.finish();
    for (auto i = 10u; i < 90; i += 10) {
        EXPECT_EQ(values.find(i), values.end());
    }

    for (auto i = 90u; i < 200; i += 10) {
        EXPECT_NE(values.find(i), values.end());
    }
}

TEST(vcd, instance_mapping) {  // NOLINT
    change_cwd();
    {
        auto db = std::make_unique<hgdb::vcd::VCDDatabase>("waveform1.vcd");
        std::unordered_set<std::string> instance_names = {"child"};
        auto const &[def_name, instance_name] = db->compute_instance_mapping(instance_names);
        EXPECT_EQ(def_name, "child");
        EXPECT_EQ(instance_name, "top.inst.");
    }
    {
        auto db = std::make_unique<hgdb::vcd::VCDDatabase>("waveform2.vcd");
        std::unordered_set<std::string> instance_names = {"child1", "child1.inst2",
                                                          "child1.inst2.inst3"};
        auto const &[def_name, instance_name] = db->compute_instance_mapping(instance_names);
        EXPECT_EQ(def_name, "child1");
        EXPECT_EQ(instance_name, "top.inst1.");
    }
}

struct get_clock_info {
    std::set<std::pair<uint64_t, int64_t>> *values;
    hgdb::RTLSimulatorClient *rtl;
    vpiHandle clk;
};

int get_cycles_clock_value(p_cb_data cb_data) {
    auto *user_data = cb_data->user_data;
    auto *info = reinterpret_cast<get_clock_info *>(user_data);
    auto time = info->rtl->get_simulation_time();
    auto value = info->rtl->get_value(info->clk);
    info->values->emplace(std::make_pair(time, *value));
    return 0;
}

TEST(replay, clk_callback_waveform3) {  // NOLINT
    change_cwd();
    auto db = std::make_unique<hgdb::vcd::VCDDatabase>("waveform3.vcd");
    auto vpi_ = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));
    auto *vpi = vpi_.get();
    hgdb::RTLSimulatorClient rtl(std::move(vpi_));
    // need to add some callbacks before hand it over to the engine
    constexpr auto clk_name = "top.clk";
    auto *clk = vpi->vpi_handle_by_name(const_cast<char *>(clk_name), nullptr);
    EXPECT_NE(clk, nullptr);

    std::set<std::pair<uint64_t, int64_t>> values;
    get_clock_info info{.values = &values, .rtl = &rtl, .clk = clk};

    s_cb_data cb{.reason = cbValueChange,
                 .cb_rtn = get_cycles_clock_value,
                 .obj = clk,
                 .user_data = reinterpret_cast<char *>(&info)};

    hgdb::replay::EmulationEngine engine(vpi);

    // register the CB
    auto *r = vpi->vpi_register_cb(&cb);
    EXPECT_NE(r, nullptr);

    engine.run();
    // need to check all the posedge entries
    for (uint32_t i = 5; i < 100; i += 10) {
        EXPECT_NE(values.find(std::make_pair(i, 1)), values.end());
    }
}