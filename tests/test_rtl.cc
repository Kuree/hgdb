#include <fmt/format.h>

#include "../src/rtl.hh"
#include "gtest/gtest.h"
#include "util.hh"

/*
 * module child_mod;
 * logic a;
 * logic b;
 * endmodule
 *
 * module parent_mod;
 * logic a;
 * logic b;
 *
 * child_mod inst1();
 * child_mod inst2();
 *
 * endmodule
 *
 * module top;
 * parent_mod dut();
 * endmodule
 */
class RTLModuleTest : public ::testing::Test {
public:
    const std::vector<std::string> argv = {"a", "bb", "ccc"};

protected:
    static constexpr int64_t a_value = 42;
    static constexpr int64_t b_value = 43;
    static constexpr uint64_t time = 0x12345678'9ABCDEF0;

    void SetUp() override {
        auto vpi_ = std::make_unique<MockVPIProvider>();
        auto *top = vpi_->add_module("top", "top");
        vpi_->set_top(top);
        auto *dut = vpi_->add_module("parent_mod", "top.dut");
        auto *inst1 = vpi_->add_module("child_mod", "top.dut.inst1");
        auto *inst2 = vpi_->add_module("child_mod", "top.dut.inst2");
        // add signals
        auto mods = {dut, inst1, inst2};
        for (auto *handle : mods) {
            std::string name = vpi_->vpi_get_str(vpiFullName, handle);
            // adding signal in full name
            auto *a = vpi_->add_signal(handle, name + ".a");
            auto *b = vpi_->add_signal(handle, name + ".b");
            auto *clk = vpi_->add_signal(handle, name + ".clk");
            // also set the values
            vpi_->set_signal_value(a, a_value);
            vpi_->set_signal_value(b, b_value);
            vpi_->set_signal_value(clk, 0);
        }
        // set argv
        vpi_->set_argv(argv);
        // set time
        vpi_->set_time(time);

        std::unique_ptr<hgdb::AVPIProvider> vpi = std::move(vpi_);
        client = std::make_unique<hgdb::RTLSimulatorClient>(std::vector<std::string>{"parent_mod"},
                                                            std::move(vpi));
    }

    MockVPIProvider &vpi() {
        auto *vpi = &client->vpi();
        auto *mock_vpi = reinterpret_cast<MockVPIProvider *>(vpi);
        return *mock_vpi;
    }

    std::unique_ptr<hgdb::RTLSimulatorClient> client;
};

TEST_F(RTLModuleTest, get_full_name) {  // NOLINT
    auto name = client->get_full_name("parent_mod");
    EXPECT_EQ(name, "top.dut");
    name = client->get_full_name("parent_mod.inst1");
    EXPECT_EQ(name, "top.dut.inst1");
    name = client->get_full_name("parent_mod.inst1.a");
    EXPECT_EQ(name, "top.dut.inst1.a");
    name = client->get_full_name("parent_mod.inst2.b");
    EXPECT_EQ(name, "top.dut.inst2.b");

    constexpr auto random_name = "42.43";
    name = client->get_full_name(random_name);
    // no translation here
    EXPECT_EQ(name, random_name);
}

TEST_F(RTLModuleTest, get_module_signals) {  // NOLINT
    auto mods = {"parent_mod", "parent_mod.inst1", "parent_mod.inst2"};
    for (auto const &mod_name : mods) {
        auto mod_signals = client->get_module_signals(mod_name);
        EXPECT_EQ(mod_signals.size(), 2);
        EXPECT_NE(mod_signals.find("a"), mod_signals.end());
        EXPECT_NE(mod_signals.find("b"), mod_signals.end());
        // check the cache if it's working
        // the handle count shall not increase if the result is cached
        auto handle_count = vpi().get_handle_count();
        EXPECT_EQ(client->get_module_signals(mod_name).size(), mod_signals.size());
        EXPECT_EQ(vpi().get_handle_count(), handle_count);
    }
}

TEST_F(RTLModuleTest, get_value) {  // NOLINT
    auto mods = {"parent_mod", "parent_mod.inst1", "parent_mod.inst2"};
    for (auto const &mod_name : mods) {
        auto a = fmt::format("{0}.{1}", mod_name, "a");
        auto b = fmt::format("{0}.{1}", mod_name, "b");
        auto a_value = client->get_value(a);
        auto b_value = client->get_value(b);
        EXPECT_EQ(a_value, RTLModuleTest::a_value);
        EXPECT_EQ(b_value, RTLModuleTest::b_value);
    }
}

TEST_F(RTLModuleTest, get_argv) {  // NOLINT
    auto argv = client->get_argv();
    EXPECT_EQ(argv.size(), RTLModuleTest::argv.size());
    for (auto i = 0u; i < argv.size(); i++) {
        EXPECT_EQ(argv[i], RTLModuleTest::argv[i]);
    }
}

TEST_F(RTLModuleTest, get_time) {  // NOLINT
    auto time = client->get_simulation_time();
    EXPECT_EQ(time, RTLModuleTest::time);
}

int test_cb_func(p_cb_data cb_data) {
    auto *user_data = cb_data->user_data;
    auto *int_value = reinterpret_cast<int *>(user_data);
    *int_value += 1;
    return 0;
}

TEST_F(RTLModuleTest, test_cb) {  // NOLINT
    int value = 0;
    client->add_call_back("test_cb", cbStartOfSimulation, test_cb_func, nullptr, &value);
    auto &mock_vpi = vpi();
    // trigger the callback
    // trigger a wrong one first
    mock_vpi.trigger_cb(cbEndOfSimulation);
    EXPECT_EQ(value, 0);
    constexpr int final_value = 4;
    for (int i = 1; i < final_value; i++) {
        mock_vpi.trigger_cb(cbStartOfSimulation);
        EXPECT_EQ(value, i);
    }
    // remove the callback
    client->remove_call_back("test_cb");
    // trigger again and it won't do anything
    mock_vpi.trigger_cb(cbStartOfSimulation);
    EXPECT_EQ(value, final_value);
}

TEST_F(RTLModuleTest, test_control) {  // NOLINT
    client->stop_sim(hgdb::RTLSimulatorClient::finish_value::time_location);
    client->finish_sim(hgdb::RTLSimulatorClient::finish_value::all);
    auto const &ops = vpi().vpi_ops();
    EXPECT_EQ(ops.size(), 2);
    EXPECT_EQ(ops[0], vpiStop);
    EXPECT_EQ(ops[1], vpiFinish);
}

TEST_F(RTLModuleTest, test_search_clk) {  // NOLINT
    auto values = client->get_clocks_from_design();
    EXPECT_FALSE(values.empty());
    EXPECT_EQ(values[0], "top.dut.clk");
}

int test_value_change(p_cb_data cb_data) {
    auto *user_data = cb_data->user_data;
    auto *int_value = reinterpret_cast<int *>(user_data);
    *int_value = 42;
    return 0;
}

TEST_F(RTLModuleTest, test_cb_value_change) {  // NOLINT
    // two singles, one before full name one after full name
    auto constexpr signal1 = "parent_mod.a";
    auto constexpr signal2 = "top.dut.b";

    auto &mock_vpi = vpi();
    std::string_view name1 = "top.dut.a";
    std::string_view name2 = signal2;
    auto *handle_a = mock_vpi.vpi_handle_by_name(const_cast<char *>(name1.data()), nullptr);
    auto *handle_b = mock_vpi.vpi_handle_by_name(const_cast<char *>(name2.data()), nullptr);
    mock_vpi.set_signal_value(handle_a, 0);
    mock_vpi.set_signal_value(handle_b, 0);

    // register callback
    int value1 = 0, value2 = 0;
    client->monitor_signals({signal1}, test_value_change, &value1);
    client->monitor_signals({signal2}, test_value_change, &value2);

    // set value
    mock_vpi.set_signal_value(handle_a, 1);
    mock_vpi.set_signal_value(handle_b, 1);

    EXPECT_EQ(value1, 42);
    EXPECT_EQ(value2, 42);
}