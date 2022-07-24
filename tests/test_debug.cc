#include <array>

#include "db.hh"
#include "debug.hh"
#include "gtest/gtest.h"
#include "test_util.hh"

namespace hgdb {

class DebuggerTestFriend {
public:
    explicit DebuggerTestFriend(Debugger *debugger) : debugger_(debugger) {}

    auto eval_breakpoints(const std::vector<DebugBreakPoint *> &bps) {
        return debugger_->eval_breakpoints(bps);
    }

private:
    Debugger *debugger_;
};

class InMemoryDebuggerTester : public ::testing::Test {
public:
    void SetUp() override {
        auto port = get_free_port();
        auto mock = std::make_unique<MockVPIProvider>();
        mock->set_argv({"+DEBUG_PORT=" + std::to_string(port)});

        // setting up the debug symbol table
        const auto *db_filename = ":memory:";
        auto db = std::make_unique<SQLiteDebugDatabase>(init_debug_db(db_filename));
        db->sync_schema();

        // instances
        auto *p = mock->add_module("TOP", "TOP");
        mock->set_top(p);
        for (auto i = 0; i < num_instances; i++) {
            auto def_name = fmt::format("inst{0}", i);
            store_instance(*db, i, def_name);
            // store breakpoint as well in the same location
            store_breakpoint(*db, i, i, filename, line);
            mock->add_module(def_name, fmt::format("TOP.{0}_inst", def_name));
        }

        auto db_client = std::make_unique<hgdb::DBSymbolTableProvider>(std::move(db));
        debugger_ = std::make_unique<Debugger>(std::move(mock));
        friend_ = std::make_unique<DebuggerTestFriend>(debugger_.get());
        debugger_->initialize_db(std::move(db_client));
        db_ = debugger_->db();
    }

protected:
    std::unique_ptr<Debugger> debugger_;
    std::unique_ptr<DebuggerTestFriend> friend_;

    hgdb::SymbolTableProvider *db_ = nullptr;

    static auto constexpr *filename = "test.cc";
    static auto constexpr line = 0u;
    static auto constexpr num_instances = 1000;
};

TEST_F(InMemoryDebuggerTester, parallel_eval) {
    auto breakpoints = db_->get_breakpoints(filename, line);
    for (auto const &bp : breakpoints) {
        debugger_->scheduler()->add_breakpoint(bp, bp);
    }
    auto bps = debugger_->scheduler()->next_breakpoints();
    EXPECT_EQ(bps.size(), num_instances);
    auto hits = friend_->eval_breakpoints(bps);
    EXPECT_EQ(hits.size(), num_instances);
    EXPECT_TRUE(std::all_of(hits.begin(), hits.end(), [](auto b) { return b; }));
}


class InMemoryPerfDebuggerTester : public InMemoryDebuggerTester {
public:
    void SetUp() override {
        auto port = get_free_port();
        auto mock = std::make_shared<MockVPIProvider>();
        mock->set_argv({"+DEBUG_PORT=" + std::to_string(port)});

        // setting up the debug symbol table
        const auto *db_filename = ":memory:";
        auto db = std::make_unique<SQLiteDebugDatabase>(init_debug_db(db_filename));
        db->sync_schema();

        // instances
        auto *top = mock->add_module("TOP", "TOP");
        mock->set_top(top);
        for (auto i = 0; i < num_instances; i++) {
            auto instance_name = fmt::format("inst{0}", i);
            store_instance(*db, i, instance_name);
            // store breakpoint as well in the same location
            store_breakpoint(*db, i, i, filename, line, 0, "a==1");
            auto *p = mock->add_module(instance_name, fmt::format("TOP.{0}_inst", instance_name));
            auto *v = mock->add_signal(p, "a");
            mock->set_signal_value(v, 1);
        }

        debugger_ = std::make_unique<Debugger>(std::move(mock));
        friend_ = std::make_unique<DebuggerTestFriend>(debugger_.get());



        auto db_client = std::make_unique<hgdb::DBSymbolTableProvider>(std::move(db));
        debugger_->initialize_db(std::move(db_client));
        db_ = debugger_->db();
    }

protected:
    static auto constexpr *filename = "test.cc";
    static auto constexpr line = 0u;
    static auto constexpr num_instances = 10;
};

TEST_F(InMemoryPerfDebuggerTester, eval_perf) {
    auto breakpoints = db_->get_breakpoints(filename, line);
    for (auto const &bp : breakpoints) {
        debugger_->scheduler()->add_breakpoint(bp, bp);
    }
    for (auto i = 0; i < 10000; i++) {
        debugger_->scheduler()->start_breakpoint_evaluation();
        auto bps = debugger_->scheduler()->next_breakpoints();
        EXPECT_EQ(bps.size(), num_instances);
        auto hits = friend_->eval_breakpoints(bps);
        EXPECT_EQ(hits.size(), num_instances);
        EXPECT_TRUE(std::all_of(hits.begin(), hits.end(), [](auto b) { return b; }));
    }
}

TEST_F(InMemoryPerfDebuggerTester, debugger_misc_perf) {
    auto breakpoints = db_->get_breakpoints(filename, line);
    // change the condition
    for (auto &bp: breakpoints) {
        bp.condition = "0";
    }
    for (auto const &bp : breakpoints) {
        debugger_->scheduler()->add_breakpoint(bp, bp);
    }
    for (auto i = 0; i < 10000; i++) {
        debugger_->eval();
    }
}
}  // namespace hgdb
