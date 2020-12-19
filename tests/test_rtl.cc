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

    void SetUp() override {
        auto vpi_ = std::make_unique<MockVPIProvider>();
        auto top = vpi_->add_module("top", "top");
        vpi_->set_top(top);
        auto dut = vpi_->add_module("parent_mod", "top.dut");
        auto inst1 = vpi_->add_module("child_mod", "top.dut.inst1");
        auto inst2 = vpi_->add_module("child_mod", "top.dut.inst2");
        // add signals
        auto mods = {dut, inst1, inst2};
        for (auto handle : mods) {
            std::string name = vpi_->vpi_get_str(vpiFullName, handle);
            // adding signal in full name
            auto a = vpi_->add_signal(handle, name + ".a");
            auto b = vpi_->add_signal(handle, name + ".b");
            // also set the values
            vpi_->set_signal_value(a, a_value);
            vpi_->set_signal_value(b, b_value);
        }
        // set argv
        vpi_->set_argv(argv);

        std::unique_ptr<hgdb::AVPIProvider> vpi = std::move(vpi_);
        client = std::make_unique<hgdb::RTLSimulatorClient>(std::vector<std::string>{"parent_mod"},
                                                            std::move(vpi));
    }

protected:
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
        auto parent_signals = client->get_module_signals(mod_name);
        EXPECT_EQ(parent_signals.size(), 2);
        EXPECT_NE(parent_signals.find("a"), parent_signals.end());
        EXPECT_NE(parent_signals.find("b"), parent_signals.end());
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