#include <filesystem>
#include <random>

#include "gtest/gtest.h"
#include "schema.hh"

namespace fs = std::filesystem;

class SchemeTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto db_filename = ":memory:";
        db = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(db_filename));
        db->sync_schema();
    }

    void TearDown() override { db.reset(); }
    std::unique_ptr<hgdb::DebugDatabase> db;
};

TEST_F(SchemeTest, init_db) {  // NOLINT
    ASSERT_NE(db, nullptr);
}

TEST_F(SchemeTest, store_instance) {  // NOLINT
    EXPECT_EQ(db->count<hgdb::Instance>(), 0);
    constexpr uint32_t id = 42;
    hgdb::store_instance(*db, id, "top.mod");
    auto result = db->get_pointer<hgdb::Instance>(id);
    EXPECT_TRUE(result);
    EXPECT_EQ(result->id, id);
}