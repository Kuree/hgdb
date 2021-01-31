#include <filesystem>

#include "../../tools/hgdb-replay/vcd.hh"
#include "gtest/gtest.h"

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
