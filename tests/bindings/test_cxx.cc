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
    SymbolTable table("test");
    table.add_module(top_name);
    check_valid(table);
}

TEST(json, nested_scope) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test");
    auto *mod = table.add_module(top_name);
    auto *scope = mod->create_scope<Scope<>>();
    scope->filename = "test.sv";
    Variable var = {.name = "test", .value = "test", .rtl = true};
    auto *s = scope->create_scope<VarStmt>(var, 2, true);

    auto str = table.output();
    check_valid(table);

    // test out query as well
    hgdb::JSONSymbolTableProvider db;
    db.parse(str);
    auto bps = db.get_breakpoints(scope->filename, s->line_num);
    EXPECT_EQ(bps.size(), 1);
}

TEST(json, instance) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test");
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

TEST(json, compression) {
    using namespace hgdb::json;
    constexpr auto top_name = "mod";
    SymbolTable table("test");
    auto *mod1 = table.add_module(top_name);
    auto *scope1 = mod1->create_scope<Scope<>>();
    auto constexpr filename = __FILE__;
    scope1->filename = filename;
    auto *scope2 = scope1->create_scope<Scope<>>();
    scope2->filename = filename;
    Variable var{.name = "a", .value = "b", .rtl = true};
    auto constexpr line1 = __LINE__;
    auto *stmt = scope2->create_scope<VarStmt>(var, line1, true);
    stmt->filename = filename;
    auto constexpr line2 = __LINE__;
    stmt = scope2->create_scope<VarStmt>(var, line2, true);
    stmt->filename = filename;
    mod1->add_variable(var);

    auto str = table.output();
    check_valid(table);

    table.compress();
    str = table.output();
    check_valid(table);

    hgdb::JSONSymbolTableProvider db;
    db.parse(str);
    auto bps = db.get_breakpoints(__FILE__);
    EXPECT_EQ(bps.size(), 2);
    bps = db.get_breakpoints(filename, line1);
    EXPECT_EQ(bps.size(), 1);
    bps = db.get_breakpoints(filename, line2);
    EXPECT_EQ(bps.size(), 1);
    auto vars = db.get_context_variables(bps[0].id);
    EXPECT_EQ(vars.size(), 1);
    EXPECT_EQ(vars[0].first.name, var.name);
}
