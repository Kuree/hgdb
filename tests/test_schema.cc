#include <random>

#include "test_util.hh"

class SchemaTest : public DBTestHelper {};

TEST_F(SchemaTest, init_db) {  // NOLINT
    ASSERT_NE(db, nullptr);
}

TEST_F(SchemaTest, store_instance) {  // NOLINT
    EXPECT_EQ(db->count<hgdb::Instance>(), 0);
    constexpr uint32_t id = 42;
    hgdb::store_instance(*db, id, "top.mod");
    auto result = db->get_pointer<hgdb::Instance>(id);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->id, id);
}

TEST_F(SchemaTest, store_breakpoint) {  // NOLINT
    EXPECT_EQ(db->count<hgdb::BreakPoint>(), 0);
    constexpr uint32_t instance_id = 42;
    constexpr uint32_t breakpoint_id = 1729;
    hgdb::store_instance(*db, instance_id, "top.mod");
    hgdb::store_breakpoint(*db, breakpoint_id, instance_id, __FILE__, __LINE__);
    auto result = db->get_pointer<hgdb::BreakPoint>(breakpoint_id);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->id, breakpoint_id);
    EXPECT_EQ(result->filename, __FILE__);
}

TEST_F(SchemaTest, store_scope) {  // NOLINT
    EXPECT_EQ(db->count<hgdb::Scope>(), 0);
    constexpr uint32_t scope_id = 42;
    hgdb::store_scope(*db, scope_id, 1u, 2u, 3u, 4u);
    auto result = db->get_pointer<hgdb::Scope>(scope_id);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->breakpoints, "1 2 3 4");
}

TEST_F(SchemaTest, store_variable) {  // NOLINT
    EXPECT_EQ(db->count<hgdb::Variable>(), 0);
    constexpr uint32_t id1 = 42, id2 = 432;
    constexpr auto value1 = "top.mod.test";
    hgdb::store_variable(*db, id1, value1);
    hgdb::store_variable(*db, id2, "value", false);
    EXPECT_EQ(db->count<hgdb::Variable>(), 2);
    auto result = db->get_pointer<hgdb::Variable>(id1);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->value, value1);
    result = db->get_pointer<hgdb::Variable>(id2);
    EXPECT_TRUE(result);
    EXPECT_FALSE(result->is_rtl);
}

void add_breakpoint_var(hgdb::SQLiteDebugDatabase &db) {
    constexpr uint32_t breakpoint_id = 42, variable_id = 432, instance_id = 163;
    constexpr auto instance_name = "top.mod";
    constexpr auto value1 = "top.mod.test";
    hgdb::store_instance(db, instance_id, instance_name);
    hgdb::store_variable(db, variable_id, value1);
    hgdb::store_breakpoint(db, breakpoint_id, instance_id, __FILE__, __LINE__);
}

TEST_F(SchemaTest, store_context_variable) {  // NOLINT
    using namespace sqlite_orm;
    EXPECT_EQ(db->count<hgdb::ContextVariable>(), 0);
    constexpr uint32_t breakpoint_id = 42, variable_id = 432;
    constexpr auto name = "a";
    add_breakpoint_var(*db);
    hgdb::store_context_variable(*db, name, breakpoint_id, variable_id);
    auto results = db->get_all<hgdb::ContextVariable>(
        where(c(&hgdb::ContextVariable::breakpoint_id) == breakpoint_id));
    EXPECT_EQ(results.size(), 1);
    auto &result = results[0];
    EXPECT_EQ(result.name, name);
    EXPECT_EQ(*result.variable_id, variable_id);
}

TEST_F(SchemaTest, store_generator_variable) {  // NOLINT
    using namespace sqlite_orm;
    EXPECT_EQ(db->count<hgdb::ContextVariable>(), 0);
    constexpr uint32_t variable_id = 432, instance_id = 163;
    constexpr auto name = "a";
    add_breakpoint_var(*db);
    hgdb::store_generator_variable(*db, name, instance_id, variable_id);
    auto results = db->get_all<hgdb::GeneratorVariable>(
        where(c(&hgdb::GeneratorVariable::instance_id) == instance_id));
    EXPECT_EQ(results.size(), 1);
    auto &result = results[0];
    EXPECT_EQ(result.name, name);
    EXPECT_EQ(*result.variable_id, variable_id);
}