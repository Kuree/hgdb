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

TEST_F(DBTest, test_get_breakpoints) {  // NOLINT
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t breakpoint_id = 1729;
    constexpr uint32_t num_breakpoints = 10;
    hgdb::store_instance(*db, instance_id, "top.mod");
    // insert for breakpoints
    auto line_num = __LINE__;
    for (auto i = 0; i < num_breakpoints; i++) {
        hgdb::store_breakpoint(*db, breakpoint_id + i, instance_id, __FILE__, line_num, i + 1);
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(db);

    auto bps = client.get_breakpoints(__FILE__, line_num);
    EXPECT_EQ(bps.size(), num_breakpoints);
    bps = client.get_breakpoints(__FILE__, line_num, 1);
    EXPECT_EQ(bps.size(), 1);
    bps = client.get_breakpoints(__FILE__, line_num, num_breakpoints + 1);
    EXPECT_TRUE(bps.empty());
}

TEST_F(DBTest, test_get_breakpoint) {  // NOLINT
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t breakpoint_id = 1729;
    hgdb::store_instance(*db, instance_id, "top.mod");
    hgdb::store_breakpoint(*db, breakpoint_id, instance_id, __FILE__, __LINE__);

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(db);

    auto bp = client.get_breakpoint(breakpoint_id);
    EXPECT_TRUE(bp);
    EXPECT_EQ(bp->id, breakpoint_id);
    bp = client.get_breakpoint(breakpoint_id + 1);
    EXPECT_FALSE(bp);
}

TEST_F(DBTest, test_get_context_variable) {  // NOLINT
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t breakpoint_id = 1729;
    constexpr uint32_t num_variables = 10;
    hgdb::store_instance(*db, instance_id, "top.mod");
    hgdb::store_breakpoint(*db, breakpoint_id, instance_id, __FILE__, __LINE__);
    // insert a range of context variable to that breakpoint
    for (uint32_t i = 0; i < num_variables; i++) {
        hgdb::store_variable(*db, i, std::to_string(i), false);
        hgdb::store_context_variable(*db, "name" + std::to_string(i), breakpoint_id, i);
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(db);

    auto values = client.get_context_variables(breakpoint_id);
    EXPECT_EQ(values.size(), num_variables);
    for (uint32_t i = 0; i < values.size(); i++) {
        auto const &[context_v, v] = values[i];
        EXPECT_EQ(context_v.name, "name" + std::to_string(i));
        EXPECT_EQ(v.value, std::to_string(i));
        EXPECT_EQ(v.id, i);
    }
}

TEST_F(DBTest, test_get_generator_variable) {  // NOLINT
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t num_variables = 10;
    hgdb::store_instance(*db, instance_id, "top.mod");
    // insert a range of context variable to that breakpoint
    for (uint32_t i = 0; i < num_variables; i++) {
        hgdb::store_variable(*db, i, std::to_string(i), false);
        hgdb::store_generator_variable(*db, "name" + std::to_string(i), instance_id, i);
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(db);

    auto values = client.get_generator_variable(instance_id);
    EXPECT_EQ(values.size(), num_variables);
    for (uint32_t i = 0; i < values.size(); i++) {
        auto const &[context_v, v] = values[i];
        EXPECT_EQ(context_v.name, "name" + std::to_string(i));
        EXPECT_EQ(v.value, std::to_string(i));
        EXPECT_EQ(v.id, i);
    }
}