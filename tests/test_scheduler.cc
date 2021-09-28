#include "../src/scheduler.hh"
#include "fmt/format.h"
#include "test_util.hh"

class ReverseMockVPIProvider : public MockVPIProvider {
public:
    bool vpi_rewind(rewind_data *reverse_data) override {
        if (can_backward) {
            time_ = reverse_data->time - 2;
        }
        return can_backward;
    }

    bool can_backward = true;

    auto time() const { return time_; }
};

/*
 * module child;
 * logic[31:0] a;
 * logic[31:0] b;
 * logic clk;
 *
 * assign a = 1;  // line num 1
 * assign b = 2;  // line num 2
 *
 * endmodule
 *
 * module top;
 * logic[31:0] a;
 * logic[31:0] b;
 * logic clk;
 *
 * child inst0();
 * child inst1();
 * endmodule
 */

template <bool reverse>
class ScheduleTestBase : public ::testing::Test {
protected:
    std::unique_ptr<hgdb::RTLSimulatorClient> rtl_;
    std::unique_ptr<hgdb::DebugDatabaseClient> db_;

    void SetUp() override {
        vpi_ = reverse ? std::make_unique<ReverseMockVPIProvider>()
                       : std::make_unique<MockVPIProvider>();
        auto db = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(""));
        db->sync_schema();

        auto constexpr instances = std::array{"top", "top.inst0", "top.inst1"};
        auto constexpr def_name = std::array{"top", "child", "child"};
        auto constexpr signals = std::array{"a", "b", "clk"};
        for (auto i = 0; i < instances.size(); i++) {
            auto *handle = vpi_->add_module(def_name[i], instances[i]);
            for (auto const &signal_name : signals) {
                vpi_->add_signal(handle, signal_name);
            }
            if (i == 0) vpi_->set_top(handle);
        }
        // setting up the DB
        for (auto i = 0; i < 2; i++) {
            auto const &module_name = def_name[i + 1];
            hgdb::store_instance(*db, i, module_name);
            for (uint32_t vi = 0; vi < signals.size(); vi++) {
                auto signal_name = fmt::format("{0}.{1}", module_name, signals[vi]);
                hgdb::store_variable(*db, vi + i * 10, signal_name);
                // don't need context mapping etc since we're only interested in the breakpoint
                // scheduling
            }

            hgdb::store_breakpoint(*db, i * 10, i, "test.sv", 1);
            hgdb::store_breakpoint(*db, i * 10 + 1, i, "test.sv", 2);
        }

        rtl_ = std::make_unique<hgdb::RTLSimulatorClient>(std::move(vpi_));
        db_ = std::make_unique<hgdb::DebugDatabaseClient>(std::move(db));
    }

private:
    std::unique_ptr<MockVPIProvider> vpi_;
};

class ScheduleTestReverse : public ScheduleTestBase<true> {};
class ScheduleTestNoReverse : public ScheduleTestBase<false> {};

TEST_F(ScheduleTestReverse, test_reverse_continue) {  // NOLINT
    // don't care about single thread mode since it will be covered by multi-threading mode
    bool val1 = false, val2 = true;
    hgdb::Scheduler scheduler(rtl_.get(), db_.get(), val1, val2);
    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::ReverseBreakpointOnly);
    auto *vpi = reinterpret_cast<ReverseMockVPIProvider *>(&rtl_->vpi());
    vpi->set_time(10);

    // insert breakpoints
    auto breakpoints = db_->get_breakpoints("test.sv");
    EXPECT_EQ(breakpoints.size(), 4);
    for (auto const &bp : breakpoints) {
        scheduler.add_breakpoint(bp, bp);
    }
    scheduler.reorder_breakpoints();

    auto bps = scheduler.next_breakpoints();
    EXPECT_EQ(bps.size(), 2);
    for (auto const &bp : bps) {
        EXPECT_EQ(bp->line_num, 2);
    }
    bps = scheduler.next_breakpoints();
    EXPECT_EQ(bps.size(), 2);
    for (auto const &bp : bps) {
        EXPECT_EQ(bp->line_num, 1);
    }

    // this should be null and we revert time
    bps = scheduler.next_breakpoints();
    EXPECT_TRUE(bps.empty());
    EXPECT_EQ(vpi->time(), 10 - 2);

    bps = scheduler.next_breakpoints();
    EXPECT_EQ(bps.size(), 2);
    for (auto const &bp : bps) {
        EXPECT_EQ(bp->line_num, 2);
    }
    EXPECT_EQ(vpi->time(), 10 - 2);
}

TEST_F(ScheduleTestNoReverse, test_stepback_no_rollback) {  // NOLINT
    // don't care about single thread mode since it will be covered by multi-threading mode
    bool val1 = false, val2 = true;
    hgdb::Scheduler scheduler(rtl_.get(), db_.get(), val1, val2);
    auto *vpi = reinterpret_cast<MockVPIProvider *>(&rtl_->vpi());
    vpi->set_time(10);

    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::StepOver);
    std::set<uint32_t> ids;

    constexpr auto num_forward_bps = 3;
    for (int i = 0; i < num_forward_bps; i++) {
        auto bps = scheduler.next_breakpoints();
        EXPECT_EQ(bps.size(), 1);
        ids.emplace(bps[0]->id);
    }
    EXPECT_EQ(rtl_->get_simulation_time(), 10);
    EXPECT_EQ(ids.size(), num_forward_bps);

    // switch to step back mode
    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::StepBack);

    std::set<uint32_t> new_ids;

    for (int i = 0; i < num_forward_bps + 1; i++) {
        // since we can't roll back, it will return the first one
        auto bps = scheduler.next_breakpoints();
        EXPECT_EQ(bps.size(), 1);
        new_ids.emplace(bps[0]->id);
    }
    EXPECT_EQ(rtl_->get_simulation_time(), 10);
    EXPECT_EQ(new_ids.size(), num_forward_bps - 1);
    for (auto const id : new_ids) EXPECT_NE(ids.find(id), ids.end());
}

TEST_F(ScheduleTestReverse, test_stepback_no_rollback) {  // NOLINT
    // don't care about single thread mode since it will be covered by multi-threading mode
    bool val1 = false, val2 = true;
    hgdb::Scheduler scheduler(rtl_.get(), db_.get(), val1, val2);
    auto *vpi = reinterpret_cast<MockVPIProvider *>(&rtl_->vpi());
    vpi->set_time(10);

    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::StepOver);
    std::set<uint32_t> ids;

    constexpr auto num_forward_bps = 3;
    for (int i = 0; i < num_forward_bps; i++) {
        auto bps = scheduler.next_breakpoints();
        EXPECT_EQ(bps.size(), 1);
        ids.emplace(bps[0]->id);
    }
    EXPECT_EQ(rtl_->get_simulation_time(), 10);
    EXPECT_EQ(ids.size(), num_forward_bps);

    // switch to step back mode
    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::StepBack);

    std::set<uint32_t> new_ids;

    for (int i = 0; i < num_forward_bps; i++) {
        // since we can't roll back, it will return the first one
        auto bps = scheduler.next_breakpoints();
        EXPECT_EQ(bps.size(), 1);
        new_ids.emplace(bps[0]->id);
    }
    EXPECT_EQ(rtl_->get_simulation_time(), 10 - 2 * ((num_forward_bps - 1) / 4 + 1));
    EXPECT_EQ(new_ids.size(), num_forward_bps);

    std::set<uint32_t> diff;
    std::set_difference(ids.begin(), ids.end(), new_ids.begin(), new_ids.end(),
                        std::inserter(diff, diff.begin()));
    EXPECT_FALSE(diff.empty());
}

TEST_F(ScheduleTestNoReverse, test_stepvoer) {  // NOLINT
    bool val1 = false, val2 = true;
    hgdb::Scheduler scheduler(rtl_.get(), db_.get(), val1, val2);

    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::StepOver);

    std::set<uint32_t> ids;

    constexpr auto num_forward_bps = 4;
    for (int i = 0; i < num_forward_bps; i++) {
        auto bps = scheduler.next_breakpoints();
        EXPECT_EQ(bps.size(), 1);
        ids.emplace(bps[0]->id);
    }
    EXPECT_EQ(ids.size(), num_forward_bps);

    auto bps = scheduler.next_breakpoints();
    EXPECT_TRUE(bps.empty());
}

TEST_F(ScheduleTestNoReverse, test_continue) {  // NOLINT
    bool val1 = false, val2 = true;
    hgdb::Scheduler scheduler(rtl_.get(), db_.get(), val1, val2);

    scheduler.set_evaluation_mode(hgdb::Scheduler::EvaluationMode::BreakPointOnly);

    auto breakpoints = db_->get_breakpoints("test.sv");
    EXPECT_EQ(breakpoints.size(), 4);
    for (auto const &bp : breakpoints) {
        scheduler.add_breakpoint(bp, bp);
    }
    scheduler.reorder_breakpoints();

    auto bps = scheduler.next_breakpoints();
    EXPECT_EQ(bps.size(), 2);

    for (auto const *bp: bps)
        EXPECT_EQ(bp->line_num, 1);

    bps = scheduler.next_breakpoints();
    EXPECT_EQ(bps.size(), 2);

    for (auto const *bp: bps)
        EXPECT_EQ(bp->line_num, 2);

    bps = scheduler.next_breakpoints();
    EXPECT_TRUE(bps.empty());
}

