#include <array>

#include "../src/db.hh"
#include "gtest/gtest.h"
#include "test_util.hh"

class DBTest : public DBTestHelper {};

TEST_F(DBTest, test_scope_biuld) {  // NOLINT
    // insert scope
    constexpr uint32_t scope_id = 42;
    hgdb::store_scope(*db, scope_id, 1u, 2u, 3u, 4u);
    // transfer the db ownership
    hgdb::DBSymbolTableProvider client(std::move(db));
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
    hgdb::DBSymbolTableProvider client(std::move(db));
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
    hgdb::DBSymbolTableProvider client(std::move(db));

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
    hgdb::DBSymbolTableProvider client(std::move(db));

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
    hgdb::DBSymbolTableProvider client(std::move(db));

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
    hgdb::DBSymbolTableProvider client(std::move(db));

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
    hgdb::DBSymbolTableProvider client(std::move(db));

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
    hgdb::DBSymbolTableProvider client(std::move(db));

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
    std::map<std::string, std::string> remap = {{"/abc", "/tmp/abc"}, {"/a/", "/a/abc"}};
    hgdb::DBSymbolTableProvider client(std::move(db));

    client.set_src_mapping(remap);

    constexpr auto target1 = "/abc/1";
    auto result1 = client.resolve_filename_to_db(target1);
    EXPECT_EQ(result1, "/tmp/abc/1");
    result1 = client.resolve_filename_to_client(result1);
    EXPECT_EQ(result1, target1);

    constexpr auto target2 = "/a/1";
    auto result2 = client.resolve_filename_to_db(target2);
    EXPECT_EQ(result2, "/a/abc/1");
    result2 = client.resolve_filename_to_client(result2);
    EXPECT_EQ(result2, target2);

    constexpr auto target3 = "/usr/abc";
    auto result3 = client.resolve_filename_to_db(target3);
    EXPECT_EQ(result3, "/usr/abc");
    result3 = client.resolve_filename_to_client(result3);
    EXPECT_EQ(result3, target3);
}

TEST_F(DBTest, get_all_signal_name) {  // NOLINT
    // need to get generator and breakpoint vars
    hgdb::store_instance(*db, 0, "a");
    hgdb::store_variable(*db, 0, "a.b");
    hgdb::store_variable(*db, 1, "c");
    hgdb::store_generator_variable(*db, "d", 0, 0);
    hgdb::store_breakpoint(*db, 0, 0, "test.cc", 1);
    hgdb::store_context_variable(*db, "e", 0, 1);

    hgdb::DBSymbolTableProvider client(std::move(db));
    auto names = client.get_all_array_names();
    EXPECT_EQ(names.size(), 2);
    EXPECT_EQ(names[0], "a.b");
    EXPECT_EQ(names[1], "a.c");
}

TEST_F(DBTest, basename) {  // NOLINT
    hgdb::store_instance(*db, 0, "mod");
    hgdb::store_breakpoint(*db, 0, 0, "test.sv", 1);

    hgdb::DBSymbolTableProvider client(std::move(db));
    EXPECT_TRUE(client.use_base_name());

    auto bps = client.get_breakpoints("/test/test.sv");
    EXPECT_EQ(bps.size(), 1);
}

TEST(JSON_DB, validate) {  // NOLINT
    {
        auto constexpr *db = R"(
{
  "table": [
    {
      "type": "module",
      "scope": [],
      "name": "test",
      "variables": [],
      "line": 0
    }
  ],
  "top": "mod"
}
)";

        std::stringstream ss;
        ss << db;
        auto res = hgdb::JSONSymbolTableProvider::valid_json(ss);
        EXPECT_TRUE(res);
    }

    {
        auto constexpr *db = R"(
{
  "table": [
    {
      "type": "module",
      "scope": [],
      "name": "test",
      "variables": [],
      "line": 0
    }
  ]
}
)";
        // no top
        std::stringstream ss;
        ss << db;
        auto res = hgdb::JSONSymbolTableProvider::valid_json(ss);
        EXPECT_FALSE(res);
    }
}

class JSONDBTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto constexpr *raw_db = R"(
{
  "generator": "hgdb",
  "table": [
    {
      "type": "module",
      "name": "mod",
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.cc",
          "line": 0,
          "scope": [
            {
              "type": "decl",
              "line": 2,
              "column": 4,
              "variable": {
                "name": "var.a",
                "value": "var_a",
                "rtl": true
              }
            },
            {
              "type": "assign",
              "line": 3,
              "variable": {
                "name": "i",
                "value": "42",
                "rtl": false
              }
            },
            {
              "type": "none",
              "line": 4,
              "condition": "i == 42"
            },
            {
              "type": "assign",
              "line": 5,
              "variable": {
                "name": "i",
                "value": "43",
                "rtl": false
              }
            },
            {
              "type": "none",
              "line": 6
            }
          ]
        }
      ],
      "line": 1,
      "variables": [
        {
          "name": "array.0",
          "value": "array_0",
          "rtl": true
        },
        {
          "name": "array[1]",
          "value": "array_1",
          "rtl": true
        },
        {
          "name": "var.a",
          "value": "var_a",
          "rtl": true
        },
        {
          "name": "var.b",
          "value": "var_b",
          "rtl": true
        }
      ],
      "instances": [
        {
          "name": "inst",
          "module": "child"
        }
      ]
    },
    {
      "type": "module",
      "name": "child",
      "line": 10,
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.cc",
          "line": 0,
          "scope": [
            {
              "type": "assign",
              "line": 12,
              "variable": {
                "name": "a",
                "value": "a",
                "rtl": true
              }
            }
          ]
        }
      ],
      "variables": [
        {
          "name": "a",
          "value": "a",
          "rtl": true
        }
      ],
      "instances": [
        {
          "name": "child1",
          "module": "mod2"
        },
        {
          "name": "child2",
          "module": "mod2"
        }
      ]
    },
    {
      "type": "module",
      "line": 1,
      "name": "mod2",
      "variables": [
        {
          "name": "a",
          "value": "a_a",
          "rtl": false
        }
      ],
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.hh",
          "line": 0,
          "scope": [
            {
              "type": "assign",
              "line": 2,
              "variable": {
                "name": "a",
                "value": "a_a",
                "rtl": false
              }
            }
          ]
        }
      ]
    }
  ],
  "top": "mod"
}
)";
        db = std::make_unique<hgdb::JSONSymbolTableProvider>();
        auto res = db->parse(raw_db);
        if (!res) throw std::runtime_error("ERROR: Incorrect JSON DB");
    }

    void TearDown() override { db.reset(); }
    std::unique_ptr<hgdb::JSONSymbolTableProvider> db;
};

TEST_F(JSONDBTest, get_instance_names) {  // NOLINT
    auto res = db->get_instance_names();
    EXPECT_EQ(res.size(), 4);
    EXPECT_EQ(res[0], "mod");
    EXPECT_EQ(res[1], "mod.inst");
    EXPECT_EQ(res[2], "mod.inst.child1");
    EXPECT_EQ(res[3], "mod.inst.child2");
}

TEST_F(JSONDBTest, get_filenames) {  // NOLINT
    auto res = db->get_filenames();
    EXPECT_EQ(res.size(), 2);
    EXPECT_EQ(res[0], "hgdb.cc");
    EXPECT_EQ(res[1], "hgdb.hh");
}

TEST_F(JSONDBTest, get_breakpoints_filename) {  // NOLINT
    auto res = db->get_breakpoints("hgdb.hh");
    EXPECT_EQ(res.size(), 2);
    EXPECT_NE(res[0].id, res[1].id);
    EXPECT_EQ(res[0].line_num, res[1].line_num);
    EXPECT_EQ(res[0].filename, res[1].filename);
    EXPECT_EQ(res[0].line_num, 2);
    EXPECT_EQ(res[0].filename, "hgdb.hh");
}

TEST_F(JSONDBTest, get_breakpoints) {  // NOLINT
    auto res = db->get_breakpoints("hgdb.cc", 2);
    EXPECT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].line_num, 2);

    res = db->get_breakpoints("hgdb.cc", 2, 4);
    EXPECT_EQ(res.size(), 1);

    res = db->get_breakpoints("hgdb.cc", 2, 10);
    EXPECT_TRUE(res.empty());

    res = db->get_breakpoints("hgdb.cc", 4);
    EXPECT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].condition, "i == 42");

    res = db->get_breakpoints("hgdb.hh", 2);
    EXPECT_EQ(res.size(), 2);
}

TEST_F(JSONDBTest, get_instance) {  // NOLINT
    auto bps = db->get_breakpoints("hgdb.cc", 12);
    auto id = bps[0].id;
    auto res = db->get_instance_id(id);
    EXPECT_TRUE(res);
    auto name1 = db->get_instance_name(*res);
    EXPECT_TRUE(name1);
    auto name2 = db->get_instance_name_from_bp(id);
    EXPECT_TRUE(name2);
    EXPECT_EQ(*name1, *name2);
    EXPECT_EQ(*name1, "mod.inst");

    auto inst_id = db->get_instance_id(*name1);
    EXPECT_EQ(inst_id, *res);
}

TEST_F(JSONDBTest, get_variable) {  // NOLINT
    {
        // context variables
        auto bp_id = db->get_breakpoints("hgdb.cc", 4);
        auto res = db->get_context_variables(bp_id[0].id);
        EXPECT_EQ(res.size(), 2);
        EXPECT_EQ(res[0].first.name, "var.a");
        EXPECT_EQ(res[1].first.name, "i");
        EXPECT_EQ(res[0].second.value, "var_a");
        EXPECT_EQ(res[1].second.value, "42");
    }

    {
        // context value overwritten
        auto bp_id = db->get_breakpoints("hgdb.cc", 6);
        auto res = db->get_context_variables(bp_id[0].id);
        EXPECT_EQ(res.size(), 2);
        EXPECT_EQ(res[0].first.name, "var.a");
        EXPECT_EQ(res[1].first.name, "i");
        EXPECT_EQ(res[0].second.value, "var_a");
        EXPECT_EQ(res[1].second.value, "43");
    }

    {
        // context static values
        auto bp_id = db->get_breakpoints("hgdb.cc", 4);
        auto vars = db->get_context_static_values(bp_id[0].id);
        EXPECT_EQ(vars.size(), 1);
        EXPECT_EQ(vars.at("i"), 42);
    }

    {
        // get generator variables
        // nested instances
        auto inst_id = db->get_instance_id("mod.inst.child1");
        auto vars = db->get_generator_variable(*inst_id);
        EXPECT_EQ(vars.size(), 1);
        EXPECT_EQ(vars[0].first.name, "a");
        EXPECT_EQ(vars[0].second.value, "a_a");
    }

    {
        // get generator variables
        auto inst_id = db->get_instance_id("mod");
        auto vars = db->get_generator_variable(*inst_id);
        EXPECT_EQ(vars.size(), 4);
    }
}

TEST_F(JSONDBTest, assign) {  // NOLINT
    auto bp_id = db->get_breakpoints("hgdb.cc", 6);
    auto assigns = db->get_assigned_breakpoints("i", bp_id[0].id);
    EXPECT_EQ(assigns.size(), 2);
}

TEST_F(JSONDBTest, resolve_names) {  // NOLINT
    {
        // resolve context
        auto bp_id = db->get_breakpoints("hgdb.cc", 6);
        auto var = db->resolve_scoped_name_breakpoint("i", bp_id[0].id);
        EXPECT_TRUE(var);
        EXPECT_EQ(*var, "43");
        var = db->resolve_scoped_name_breakpoint("var.a", bp_id[0].id);
        EXPECT_TRUE(var);
        EXPECT_EQ(*var, "mod.var_a");
    }

    {
        // resolve instances
        auto inst_id = db->get_instance_id("mod");
        // notice that the underlying implementation doesn't care about [0] or .0 notation
        auto var = db->resolve_scoped_name_instance("array[0]", *inst_id);
        EXPECT_EQ(*var, "mod.array_0");

        var = db->resolve_scoped_name_instance("array.0", *inst_id);
        EXPECT_EQ(*var, "mod.array_0");

        var = db->resolve_scoped_name_instance("var.a", *inst_id);
        EXPECT_EQ(*var, "mod.var_a");
    }
}

TEST_F(JSONDBTest, get_bps) {  // NOLINT
    auto bps = db->execution_bp_orders();
    EXPECT_TRUE(bps.size() > 5);
}

TEST(json, reorder_bp) {  // NOLINT
    auto constexpr *raw_db = R"(
{
  "generator": "hgdb",
  "table": [
    {
      "type": "module",
      "name": "mod",
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.cc",
          "scope": [
            {
              "type": "decl",
              "line": 6,
              "column": 4,
              "variable": {
                "name": "var.a",
                "value": "var_a",
                "rtl": true
              }
            },
            {
              "type": "block",
              "scope": [
                {
                  "type": "none",
                  "line": 3
                },
                {
                  "type": "none",
                  "line": 2
                }
              ]
            }
          ]
        }
      ],
      "variables": [],
      "instances": []
    }
  ],
  "top": "mod"
}
)";
    hgdb::JSONSymbolTableProvider db;
    db.parse(raw_db);
    EXPECT_FALSE(db.bad());

    // read out the instances ids
    auto bps = db.get_breakpoints("hgdb.cc");
    EXPECT_EQ(bps.size(), 3);
    EXPECT_EQ(bps[0].id, 0);
    EXPECT_EQ(bps[0].line_num, 2);
    EXPECT_EQ(bps[2].id, 2);
    EXPECT_EQ(bps[2].line_num, 6);
}

TEST(json, var_merging) {  // NOLINT
    auto constexpr *raw_db = R"(
{
  "generator": "hgdb",
  "table": [
    {
      "type": "module",
      "name": "mod",
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.cc",
          "scope": [
            {
              "type": "decl",
              "line": 6,
              "column": 4,
              "variable": {
                "name": "var.a",
                "value": "var_a",
                "rtl": true
              }
            },
            {
              "type": "assign",
              "line": 6,
              "column": 4,
              "variable": {
                "name": "var.a",
                "value": "var_a",
                "rtl": true
              }
            },
            {
              "type": "decl",
              "line": 6,
              "column": 4,
              "variable": {
                "name": "var.b",
                "value": "var_b",
                "rtl": true
              }
            },
            {
              "type": "assign",
              "line": 6,
              "column": 4,
              "variable": {
                "name": "var.b",
                "value": "var_b",
                "rtl": true
              }
            }
          ]
        }
      ],
      "variables": [],
      "instances": []
    }
  ],
  "top": "mod"
}
)";
    hgdb::JSONSymbolTableProvider db;
    db.parse(raw_db);
    EXPECT_FALSE(db.bad());

    // read out the instances ids
    auto bps = db.get_breakpoints("hgdb.cc");
    EXPECT_EQ(bps.size(), 2);
    EXPECT_EQ(bps[0].id, 0);
    EXPECT_EQ(bps[0].line_num, 6);
    EXPECT_EQ(bps[1].id, 1);
    EXPECT_EQ(bps[1].line_num, 6);

    // get context variables
    auto vars = db.get_context_variables(1);
    EXPECT_EQ(vars.size(), 2);
    std::unordered_set<std::string> var_names;
    for (auto const &[_, value] : vars) {
        var_names.emplace(value.value);
    }
    EXPECT_NE(var_names.find("var_a"), var_names.end());
    EXPECT_NE(var_names.find("var_b"), var_names.end());
}

TEST(json, var_ref) {  // NOLINT
    auto constexpr *raw_db = R"(
{
  "generator": "hgdb",
  "table": [
    {
      "type": "module",
      "name": "mod",
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.cc",
          "scope": [
            {
              "type": "none",
              "line": 8
            },
            {
              "type": "decl",
              "line": 6,
              "column": 4,
              "variable": "42"
            },
            {
              "type": "assign",
              "line": 6,
              "column": 4,
              "variable": "420"
            }
          ]
        }
      ],
      "variables": [
        "42",
        {
          "name": "var.c",
          "value": "var_c",
          "rtl": true
        }
      ],
      "instances": []
    }
  ],
  "top": "mod",
  "variables": [
    {
      "name": "var.a",
      "value": "var_a",
      "rtl": true,
      "id": "42"
    },
    {
      "name": "var.b",
      "value": "var_b",
      "rtl": true,
      "id": "420"
    }
  ]
}
)";
    hgdb::JSONSymbolTableProvider db;
    db.parse(raw_db);
    EXPECT_FALSE(db.bad());

    // read out the instances ids
    auto bps = db.get_breakpoints("hgdb.cc");
    EXPECT_EQ(bps.size(), 3);
    EXPECT_EQ(bps[0].id, 0);
    EXPECT_EQ(bps[0].line_num, 6);
    EXPECT_EQ(bps[1].id, 1);
    EXPECT_EQ(bps[1].line_num, 6);
    EXPECT_EQ(bps[2].id, 2);
    EXPECT_EQ(bps[2].line_num, 8);

    // get context variables
    {
        auto vars = db.get_context_variables(2);
        EXPECT_EQ(vars.size(), 2);
        std::unordered_set<std::string> var_names;
        for (auto const &[_, value] : vars) {
            var_names.emplace(value.value);
        }
        EXPECT_NE(var_names.find("var_a"), var_names.end());
        EXPECT_NE(var_names.find("var_b"), var_names.end());
    }
    // auto gen vars
    {
        auto vars = db.get_generator_variable(0);
        EXPECT_EQ(vars.size(), 2);
        std::unordered_set<std::string> var_names;
        for (auto const &[_, value] : vars) {
            var_names.emplace(value.value);
        }
        EXPECT_NE(var_names.find("var_a"), var_names.end());
        EXPECT_NE(var_names.find("var_c"), var_names.end());
    }
}

TEST(json, var_index_assign) {  // NOLINT
    auto constexpr *raw_db = R"(
{
  "generator": "hgdb",
  "table": [
    {
      "type": "module",
      "name": "mod",
      "scope": [
        {
          "type": "block",
          "filename": "hgdb.cc",
          "scope": [
            {
              "type": "assign",
              "line": 6,
              "column": 4,
              "variable": "42",
              "index": {
                "var": "420",
                "min": 0,
                "max": 15
              }
            },
            {
              "type": "assign",
              "line": 6,
              "column": 4,
              "variable": {
                "name": "var[4]",
                "value": "var[4]",
                "rtl": true
              }
            }
          ]
        }
      ],
      "variables": [],
      "instances": []
    }
  ],
  "top": "mod",
  "variables": [
    {
      "name": "var",
      "value": "var",
      "rtl": true,
      "id": "42"
    },
    {
      "name": "addr",
      "value": "addr",
      "rtl": true,
      "id": "420"
    }
  ]
}
)";
    hgdb::JSONSymbolTableProvider db;
    db.parse(raw_db);
    EXPECT_FALSE(db.bad());

    auto assigns = db.get_assigned_breakpoints("var[4]", 1);
    EXPECT_EQ(assigns.size(), 2);
    {
        auto [id, value, cond] = assigns[0];
        EXPECT_EQ(cond, "addr == 4");
        EXPECT_EQ(value, "var[4]");
    }

    {
        auto [id, value, cond] = assigns[1];
        EXPECT_EQ(cond, "");
        EXPECT_EQ(value, "var[4]");
    }
}

TEST(json, attributes) {  // NOLINT
    auto constexpr *raw_db = R"(
{
  "generator": "hgdb",
  "table": [
    {
      "type": "module",
      "name": "mod",
      "scope": [],
      "variables": [],
      "instances": []
    }
  ],
  "top": "mod",
  "variables": [],
  "attributes": [
    {
      "name": "clock",
      "value": "mod.clock"
    }
  ]
}
)";
    hgdb::JSONSymbolTableProvider db;
    db.parse(raw_db);
    EXPECT_FALSE(db.bad());

    auto res = db.get_annotation_values("clock");
    EXPECT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "mod.clock");
}
