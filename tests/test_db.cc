#include <array>

#include "../src/db.hh"
#include "gtest/gtest.h"
#include "util.hh"

class DBTest : public DBTestHelper {};

TEST_F(DBTest, test_scope_biuld) {  // NOLINT
    // insert scope
    constexpr uint32_t scope_id = 42;
    hgdb::store_scope(*db, scope_id, 1u, 2u, 3u, 4u);
    // transfer the db ownership
    hgdb::DebugDatabaseClient client(std::move(db));
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
    hgdb::DebugDatabaseClient client(std::move(db));
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
    hgdb::DebugDatabaseClient client(std::move(db));

    auto bps = client.get_breakpoints(__FILE__, line_num);
    EXPECT_EQ(bps.size(), num_breakpoints);
    bps = client.get_breakpoints(__FILE__, line_num, 1);
    EXPECT_EQ(bps.size(), 1);
    bps = client.get_breakpoints(__FILE__, line_num, num_breakpoints + 1);
    EXPECT_TRUE(bps.empty());

    // all breakpoints in a file
    bps = client.get_breakpoints(__FILE__);
    EXPECT_EQ(bps.size(), num_breakpoints);
}

TEST_F(DBTest, test_get_breakpoint) {  // NOLINT
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t breakpoint_id = 1729;
    hgdb::store_instance(*db, instance_id, "top.mod");
    hgdb::store_breakpoint(*db, breakpoint_id, instance_id, __FILE__, __LINE__);

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(std::move(db));

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
    hgdb::DebugDatabaseClient client(std::move(db));

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
    // insert a range of generator variable to that breakpoint
    for (uint32_t i = 0; i < num_variables; i++) {
        hgdb::store_variable(*db, i, std::to_string(i), false);
        hgdb::store_generator_variable(*db, "name" + std::to_string(i), instance_id, i);
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(std::move(db));

    auto values = client.get_generator_variable(instance_id);
    EXPECT_EQ(values.size(), num_variables);
    for (uint32_t i = 0; i < values.size(); i++) {
        auto const &[context_v, v] = values[i];
        EXPECT_EQ(context_v.name, "name" + std::to_string(i));
        EXPECT_EQ(v.value, std::to_string(i));
        EXPECT_EQ(v.id, i);
    }
}

TEST_F(DBTest, test_get_annotation_values) {  // NOLINT
    constexpr auto name = "name";
    constexpr std::array values{"1", "2", "3"};
    for (auto const &value : values) {
        hgdb::store_annotation(*db, name, value);
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(std::move(db));

    auto a_values = client.get_annotation_values(name);
    for (auto const &value : values) {
        EXPECT_NE(std::find(a_values.begin(), a_values.end(), value), a_values.end());
    }
}

TEST_F(DBTest, test_get_variable_prefix) {  // NOLINT
    // test out automatic full name computation
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t num_variables = 10;
    constexpr uint32_t breakpoint_id = 1729;
    hgdb::store_instance(*db, instance_id, "top.mod");
    hgdb::store_breakpoint(*db, breakpoint_id, instance_id, __FILE__, __LINE__);
    // insert a range of generator variable to that breakpoint
    uint32_t id_count = 0;
    for (uint32_t i = 0; i < num_variables; i++) {
        hgdb::store_variable(*db, id_count, std::to_string(i), true);
        hgdb::store_generator_variable(*db, "name" + std::to_string(i), instance_id, id_count);
        id_count++;
    }
    // insert a range of context variable to that breakpoint
    for (uint32_t i = 0; i < num_variables; i++) {
        hgdb::store_variable(*db, id_count, std::to_string(i), false);
        hgdb::store_context_variable(*db, "name" + std::to_string(i), breakpoint_id, id_count);
        id_count++;
    }

    // transfer the db ownership
    hgdb::DebugDatabaseClient client(std::move(db));

    auto gen_values = client.get_generator_variable(instance_id);
    EXPECT_EQ(gen_values.size(), num_variables);
    for (uint32_t i = 0; i < gen_values.size(); i++) {
        auto const &[context_v, v] = gen_values[i];
        EXPECT_EQ(context_v.name, "name" + std::to_string(i));
        EXPECT_EQ(v.value, "top.mod." + std::to_string(i));
    }
    auto context_values = client.get_context_variables(breakpoint_id);
    EXPECT_EQ(context_values.size(), num_variables);
    for (uint32_t i = 0; i < context_values.size(); i++) {
        auto const &[context_v, v] = context_values[i];
        EXPECT_EQ(context_v.name, "name" + std::to_string(i));
        EXPECT_EQ(v.value, std::to_string(i));
    }
}

TEST_F(DBTest, resolve_path) {  // NOLINT
    std::unordered_map<std::string, std::string> remap = {{"/abc", "/tmp/abc"}, {"/a/", "/a/abc"}};
    hgdb::DebugDatabaseClient client(std::move(db));

    client.set_src_mapping(remap);

    auto result1 = client.resolve_filename("/abc/1");
    EXPECT_EQ(result1, "/tmp/abc/1");

    auto result2 = client.resolve_filename("/a/1");
    EXPECT_EQ(result2, "/a/abc/1");

    auto result3 = client.resolve_filename("/tmp/abc");
    EXPECT_EQ(result3, "/tmp/abc");
}