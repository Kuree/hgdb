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
    }

    void TearDown() override { db.reset(); }
    std::unique_ptr<hgdb::DebugDatabase> db;
};

TEST_F(SchemeTest, init_db) {  // NOLINT
    ASSERT_NE(db, nullptr);
}