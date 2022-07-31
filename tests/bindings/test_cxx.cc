#include "../../include/json.hh"
#include "../../src/db.hh"
#include "gtest/gtest.h"

void check_valid(hgdb::json::SymbolTable &table) {
    auto str = table.output();
    std::stringstream ss;
    ss << str;
    auto res = hgdb::JSONSymbolTableProvider::valid_json(ss);
    EXPECT_TRUE(res);
}

TEST(json, module) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test", top_name);
    table.add_module(top_name);
    check_valid(table);
}

TEST(json, nested_scope) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test", top_name);
    auto *mod = table.add_module(top_name);
    auto *scope = mod->create_scope<Scope<>>();
    scope->filename_ = "test.sv";
    Variable var = {.name = "test", .value = "test", .rtl = true};
    auto *s = scope->create_scope<VarStmt>(var, true);
    s->line_num = 2;

    auto str = table.output();
    check_valid(table);

    // test out query as well
    hgdb::JSONSymbolTableProvider db;
    db.parse(str);
    auto bps = db.get_breakpoints(scope->filename_, s->line_num);
    EXPECT_EQ(bps.size(), 1);
}

TEST(json, instance) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test", top_name);
    auto *mod1 = table.add_module(top_name);
    auto *mod2 = table.add_module("mod2");
    mod1->add_instance("inst1", mod2);
    Variable var = {.name = "test", .value = "test", .rtl = true};
    mod1->add_variable(var);

    auto str = table.output();
    check_valid(table);

    // test out query as well
    hgdb::JSONSymbolTableProvider db;
    db.parse(str);
    auto names = db.get_instance_names();
    EXPECT_EQ(names.size(), 2);

    auto vars = db.get_generator_variable(0);
    EXPECT_EQ(vars.size(), 1);
}
