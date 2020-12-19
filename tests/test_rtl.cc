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
protected:
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
            vpi_->add_signal(handle, name + ".a");
            vpi_->add_signal(handle, name + ".b");
        }
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

    constexpr auto random_name = "42.43";
    name = client->get_full_name(random_name);
    // no translation here
    EXPECT_EQ(name, random_name);
}

TEST_F(RTLModuleTest, get_module_signals) {  // NOLINT
}
