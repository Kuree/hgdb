#include <filesystem>
#include <random>

#include "gtest/gtest.h"
#include "schema.hh"

namespace fs = std::filesystem;

class SchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto db_filename = ":memory:";
        db = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(db_filename));
        db->sync_schema();
    }

    void TearDown() override { db.reset(); }
    std::unique_ptr<hgdb::DebugDatabase> db;
};

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

TEST_F(SchemaTest, store_scope) {   // NOLINT
    EXPECT_EQ(db->count<hgdb::Scope>(), 0);
    constexpr uint32_t scope_id = 42;
    hgdb::store_scope(*db, scope_id, 1u, 2u, 3u, 4u);
    auto result = db->get_pointer<hgdb::Scope>(scope_id);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->breakpoints, "1 2 3 4");
}