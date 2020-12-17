#ifndef HGDB_UTIL_HH
#define HGDB_UTIL_HH

#include "gtest/gtest.h"
#include "schema.hh"

class DBTestHelper : public ::testing::Test {
protected:
    void SetUp() override {
        auto db_filename = ":memory:";
        db = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(db_filename));
        db->sync_schema();
    }

    void TearDown() override { db.reset(); }
    std::unique_ptr<hgdb::DebugDatabase> db;
};

#endif  // HGDB_UTIL_HH
