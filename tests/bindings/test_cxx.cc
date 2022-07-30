#include "../../include/json.hh"
#include "../../src/db.hh"
#include "gtest/gtest.h"

TEST(json, module) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test", top_name);
    auto *mod = table.add_module(top_name);
    auto str = table.output();
    std::stringstream ss;
    ss << str;
    auto res = hgdb::JSONSymbolTableProvider::valid_json(ss);
    EXPECT_TRUE(res);
}