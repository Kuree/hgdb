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
        debugger_ = std::make_unique<Debugger>(std::move(mock));
        friend_ = std::make_unique<DebuggerTestFriend>(debugger_.get());

        // setting up the debug symbol table
        const auto *db_filename = ":memory:";
        auto db = std::make_unique<SQLiteDebugDatabase>(init_debug_db(db_filename));
        db->sync_schema();

        // instances
        for (auto i = 0; i < num_instances; i++) {
            store_instance(*db, i, fmt::format("inst{0}", i));
            // store breakpoint as well in the same location
            store_breakpoint(*db, i, i, filename, line);
        }

        auto db_client = std::make_unique<hgdb::DBSymbolTableProvider>(std::move(db));
        debugger_->initialize_db(std::move(db_client));
        db_ = debugger_->db();
    }

protected:
    std::unique_ptr<Debugger> debugger_;
    std::unique_ptr<DebuggerTestFriend> friend_;

    hgdb::SymbolTableProvider *db_;

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

}  // namespace hgdb
