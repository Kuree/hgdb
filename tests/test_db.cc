#include "../src/db.hh"
#include "gtest/gtest.h"
#include "util.hh"

class DBTest : public DBTestHelper {};

TEST_F(DBTest, test_scope_biuld) {  // NOLINT
    // insert scope
    constexpr uint32_t scope_id = 42;
    hgdb::store_scope(*db, scope_id, 1u, 2u, 3u, 4u);
    // transfer the db ownership
    hgdb::DebugDatabaseClient client(db);
    auto const &bps = client.execution_bp_orders();
    EXPECT_EQ(bps.size(), 4);
    for (uint32_t i = 1; i < 5; i++) {
        EXPECT_EQ(bps[i - 1], i);
    }
}

TEST_F(DBTest, test_scope_biuld_raw) {  // NOLINT
    // no scope inserted, just breakpoints
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t breakpoint_id = 1729;
    hgdb::store_instance(*db, instance_id, "top.mod");
    // insert for breakpoints
    for (auto i = 0; i < 4; i++) {
        hgdb::store_breakpoint(*db, breakpoint_id + i, instance_id, __FILE__, i + 1);
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(db);
    auto const &bps = client.execution_bp_orders();
    EXPECT_EQ(bps.size(), 4);
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_EQ(bps[i], i + breakpoint_id);
    }
}