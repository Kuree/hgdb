#include <filesystem>
#include <random>

#include "gtest/gtest.h"
#include "schema.hh"

namespace fs = std::filesystem;

class SchemeTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto temp_dir = fs::temp_directory_path();
        auto filename = random_filename() + ".db";
        db_filename = temp_dir / filename;
        db = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(db_filename));
    }

    void TearDown() override {
        db.reset();
        if (fs::exists(db_filename)) {
            fs::remove(db_filename);
        }
    }

    std::unique_ptr<hgdb::DebugDatabase> db;
    std::string db_filename;

private:
    static std::string random_filename() {
        std::random_device rd;
        std::mt19937 rng(rd());
        std::stringstream ss;
        std::uniform_int_distribution<char> uni(0, 26 - 1);
        for (auto i = 0; i < 12; i++) {
            ss << static_cast<char>('a' + uni(rng));
        }
        return ss.str();
    }
};

TEST_F(SchemeTest, init_db) {  // NOLINT
    std::cout << db_filename << std::endl;
    ASSERT_TRUE(fs::exists(db_filename));
    ASSERT_NE(db, nullptr);
}