#include <chrono>
#include <thread>

#include "../src/db.hh"
#include "../src/debug.hh"
#include "fmt/format.h"
#include "test_util.hh"

/*
 * module top;
 *
 * logic a, clk, rst, b;
 * logic[2:0] addr;
 * logic[7:0] value;
 *
 * mod dut (.*);
 *
 * endmodule
 *
 * module mod (input  logic a,
 *             input  logic clk,
 *             input  logic rst,
 *             input  logic[2:0] addr,
 *             input  logic[7:0] value,
 *             output logic b);
 * // notice the SSA transform here
 * // if (a)
 * //    c = ~a;
 * // else
 * //    c = 0;
 * // c = a;
 * logic c, c_0, c_1, c_2, c_3,;
 * assign c_0 = ~a; // a = a  en: a           -> c = ~a  | ln: 2
 * assign c_1 = 0;  // a = a  en: ~a          -> c = 0   | ln: 4
 * assign c_2 = a? c_0: c_1; // a = a en: 1   -> if (a)  | ln: 1
 * assign c_3 = a; // a = a c = c_2 en: 1     -> c = a   | ln: 5
 *
 * // array
 * logic [7:0][3:0] array;
 *
 * // this is just for testing trigger
 * logic d, e;
 * // always_comb
 * //     d = e
 * assign d = e; // d = d e = e en: 1 trigger e   -> d = e | ln: 6
 *
 * always_ff @(posedge clk, posedge rst) begin
 *     if (rst)
 *         b <= 0;    // rst = rst en: rst
 *     else
 *         b <= c;    // rst = rst en: ~rst
 * end
 *
 * always_ff @(posedge clk) begin
 *   array[addr] <= value;
 * end
 *
 * endmodule
 *
 */

auto setup_sqlite_db_vpi(MockVPIProvider &vpi) {
    using namespace hgdb;
    // create a mock design, see comments above
    const auto *db_filename = ":memory:";
    auto db = std::make_unique<SQLiteDebugDatabase>(init_debug_db(db_filename));
    db->sync_schema();
    // store
    auto variables = std::vector<std::string>{"clk", "rst", "a", "b", "addr", "value", "array"};
    // some global variables to make things easier to track
    std::set<uint32_t> context_array_breakpoint_ids;
    // notice that mod and mod2 have the same definition
    // we do so to test out multiple hierarchy mapping
    std::vector<std::pair<std::string, std::string>> instance_names = {
        {"top", "top"}, {"mod", "top.dut"}, {"mod2", "top.dut2"}};
    std::vector<vpiHandle> instance_handles;
    for (uint32_t instance_id = 0; instance_id < 3; instance_id++) {
        auto const &[def_name, inst_name] = instance_names[instance_id];
        auto *handle = vpi.add_module(def_name, inst_name);
        instance_handles.emplace_back(handle);
        if (instance_id == 0) {
            vpi.set_top(handle);
        } else {
            // store the dut to db using its def name
            store_instance(*db, instance_id, def_name);
        }
    }

    uint32_t var_id = 0;
    for (auto dut_id = 1; dut_id <= 2; dut_id++) {
        // we are only interested in dut and dut2
        std::map<std::string, uint32_t> variable_ids;
        std::map<std::string, vpiHandle> variable_handles;
        auto const &[dut_instance_name, dut_instance_full_name] = instance_names[dut_id];
        auto *dut_instance_handle = instance_handles[dut_id];
        for (const auto &name : variables) {
            auto var_name = fmt::format("{0}.{1}", dut_instance_name, name);
            auto var_full_name = fmt::format("{0}.{1}", dut_instance_full_name, name);
            store_variable(*db, var_id, var_name);
            variable_ids.emplace(name, var_id);
            auto *handle = vpi.add_signal(dut_instance_handle, var_full_name);
            variable_handles.emplace(name, handle);
            // generator variable
            store_generator_variable(*db, name, dut_id, var_id);
            var_id++;
        }

        auto temp_variables = {"c", "c_0", "c_1", "c_2", "c_3", "d", "e", "f"};
        for (auto const &name : temp_variables) {
            auto var_name = fmt::format("{0}.{1}", dut_instance_name, name);
            auto var_full_name = fmt::format("{0}.{1}", dut_instance_full_name, name);
            auto *handle = vpi.add_signal(dut_instance_handle, var_full_name);
            variable_handles[name] = handle;
            // this is still valid variable, just not showing up in the context/generator map
            store_variable(*db, var_id, var_name);
            variable_ids[name] = var_id++;
        }

        // now we need to deal with breakpoints
        constexpr auto filename = "/tmp/test.py";
        auto base_id = (dut_id - 1) * 7;
        // assign c_0 = ~a; // a = a  en: a           -> c = ~a
        store_breakpoint(*db, 0 + base_id, dut_id, filename, 2, 0, "a");
        store_context_variable(*db, "a", 0 + base_id, variable_ids.at("a"));
        store_assignment(*db, "c", "c_0", 0 + base_id);
        // assign c_1 = 0;  // a = a  en: ~a          -> c = 0
        store_breakpoint(*db, 1 + base_id, dut_id, filename, 4, 0, "!a");
        store_context_variable(*db, "a", 1 + base_id, variable_ids.at("a"));
        store_assignment(*db, "c", "c_1", 1 + base_id);
        // assign c_2 = a? c_0: c_1; // a = a en: 1   -> if (a)
        store_breakpoint(*db, 2 + base_id, dut_id, filename, 1, 0, "1");
        store_context_variable(*db, "a", 2 + base_id, variable_ids.at("a"));
        // notice that the following line is never needed, because this is
        // the actual assignment created by SSA
        // store_assignment(*db, "c", "c_2", 2 + base_id);
        // assign c_3 = a; // a = a, c = c_2 en: 1     -> c = a
        store_breakpoint(*db, 3 + base_id, dut_id, filename, 5, 0, "1");
        store_context_variable(*db, "a", 3 + base_id, variable_ids.at("a"));
        store_context_variable(*db, "c", 3 + base_id, variable_ids.at("c_2"));
        store_assignment(*db, "c", "c_3", 3 + base_id);
        // assign d = e;
        store_breakpoint(*db, 4 + base_id, dut_id, filename, 6, 0, "", "e");
        store_context_variable(*db, "d", 4 + base_id, variable_ids.at("d"));
        store_context_variable(*db, "e", 4 + base_id, variable_ids.at("e"));
        // a new value f. notice that f is solely introduced for testing delayed breakpoint
        // it does not have corresponding value in the source code
        store_breakpoint(*db, 5 + base_id, dut_id, filename, 8, 0);
        store_context_variable(*db, "f", 5 + base_id, variable_ids.at("f"));
        // delaying e's value
        store_context_variable(*db, "f0", 5 + base_id, variable_ids.at("f"), true);

        // array assignment
        store_breakpoint(*db, 6 + base_id, dut_id, filename, 7, 0, "1");
        store_assignment(*db, "array", "array", 6 + base_id);
        // also store array to gen context
        for (auto i = 0; i < 4; i++) {
            auto name = fmt::format("array[{0}]", i);
            store_variable(*db, var_id, name);
            store_generator_variable(*db, name, dut_id, var_id);
            // and context without delay mode
            store_context_variable(*db, name, 6 + base_id, var_id, false);
            var_id++;
        }
        context_array_breakpoint_ids.emplace(6 + base_id);
        // set values
        // we simulate the case where a is 1
        // in this case we have
        // c_0 = ~a -> 0
        // c_1 = 0
        // c_2 = 0
        // c_3 = 1
        // notice that we don't have b's value yet, so when we query we will have ERROR as the
        // result
        vpi.set_signal_value(variable_handles.at("c_0"), 0);
        vpi.set_signal_value(variable_handles.at("c_1"), 0);
        vpi.set_signal_value(variable_handles.at("c_2"), 0);
        vpi.set_signal_value(variable_handles.at("c_3"), 1);
        vpi.set_signal_value(variable_handles.at("a"), 1);

        // set signal dim and then set the value
        vpi.set_signal_dim(variable_handles.at("array"), 4);
        vpi.set_signal_value(variable_handles.at("value"), 4);
        vpi.set_signal_value(variable_handles.at("addr"), 1);
    }

    auto db_client = std::make_unique<hgdb::DBSymbolTableProvider>(std::move(db));
    for (auto const id : context_array_breakpoint_ids) {
        db_client->set_context_delay_var(id, "array[3]", "array[3]");
    }

    return db_client;
}

std::unique_ptr<hgdb::SymbolTableProvider> setup_json_db_vpi(MockVPIProvider &vpi) {
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
          "filename": "/tmp/test.py",
          "scope": [
            {
              "type": "assign",
              "line": 2,
              "condition": "a",
              "variable": {
                "name": "c",
                "value": "c_0",
                "rtl": true
              }
            },
            {
              "type": "assign",
              "line": 4,
              "condition": "!a",
              "variable": {
                "name": "c",
                "value": "c_1",
                "rtl": true
              }
            },
            {
              "type": "assign",
              "line": 1,
              "variable": {
                "name": "c",
                "value": "0",
                "rtl": false
              }
            },
            {
              "type": "assign",
              "line": 5,
              "variable": {
                "name": "c",
                "value": "c_3",
                "rtl": true
              }
            }
          ]
        },
        {
          "type": "block",
          "filename": "/tmp/test.py",
          "scope": [
            {
              "type": "decl",
              "line": 10,
              "variable": {
                "name": "array",
                "value": "array",
                "rtl": true
              }
            },
            {
              "type": "assign",
              "line": 11,
              "variable": {
                "name": "array[0]",
                "value": "array[0]",
                "rtl": true,
                "type": "delay",
                "depth": 4
              }
            },
            {
              "type": "none",
              "line": 12
            },
            {
              "type": "assign",
              "line": 13,
              "variable": {
                "name": "array",
                "value": "array",
                "rtl": true,
                "type": "delay"
              },
              "index": {
                "var": {
                  "name": "addr",
                  "value": "value",
                  "rtl": true
                },
                "min": 0,
                "max": 3
              }
            },
            {
              "type": "none",
              "line": 14
            }
          ]
        }
      ],
      "variables": [
        {
          "name": "a",
          "value": "a",
          "rtl": true
        },
        {
          "name": "clk",
          "value": "clk",
          "rtl": true
        },
        {
          "name": "addr",
          "value": "addr",
          "rtl": true
        }
      ],
      "instances": []
    }
  ],
  "top": "mod"
}
)";
    // notice that the JSON db is slightly different from the SQLite DB
    auto variables = std::vector<std::string>{"clk", "rst", "a", "b", "addr", "array"};
    // some global variables to make things easier to track
    std::set<uint32_t> context_array_breakpoint_ids;
    // notice that mod and mod2 have the same definition
    // we do so to test out multiple hierarchy mapping
    std::vector<std::pair<std::string, std::string>> instance_names = {{"top", "top"},
                                                                       {"mod", "top.dut"}};
    vpiHandle dut_instance_handle = nullptr;
    for (auto const &[def_name, inst_name] : instance_names) {
        bool top = !dut_instance_handle;
        dut_instance_handle = vpi.add_module(def_name, inst_name);
        if (top) {
            vpi.set_top(dut_instance_handle);
        }
    }

    auto rtl_variables = {"a", "c", "c_0", "c_1",   "c_2",   "c_3",
                          "d", "e", "f",   "array", "value", "addr"};
    auto constexpr dut_instance_full_name = "top.dut";
    std::unordered_map<std::string, vpiHandle> variable_handles;
    for (auto const &name : rtl_variables) {
        auto var_full_name = fmt::format("{0}.{1}", dut_instance_full_name, name);
        auto *handle = vpi.add_signal(dut_instance_handle, var_full_name);
        variable_handles[name] = handle;
    }

    vpi.set_signal_value(variable_handles.at("c_0"), 0);
    vpi.set_signal_value(variable_handles.at("c_1"), 0);
    vpi.set_signal_value(variable_handles.at("c_2"), 0);
    vpi.set_signal_value(variable_handles.at("c_3"), 1);
    vpi.set_signal_value(variable_handles.at("a"), 1);

    // set signal dim and then set the value
    vpi.set_signal_dim(variable_handles.at("array"), 4);
    vpi.set_signal_value(variable_handles.at("value"), 4);
    vpi.set_signal_value(variable_handles.at("addr"), 1);

    auto db = std::make_unique<hgdb::JSONSymbolTableProvider>();
    db->parse(raw_db);
    return db;
}

auto set_mock() {
    auto vpi = std::make_unique<MockVPIProvider>();

    return vpi;
}

// NOLINTNEXTLINE
int main(int argc, char *argv[]) {
    // can only run if there is a +DEBUG_LOG flag
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    bool should_run = false;
    bool no_eval = false;
    bool rewind = false;
    bool use_json = false;
    for (auto const &arg : args) {
        if (arg == "+DEBUG_LOG") {
            should_run = true;
        } else if (arg == "+NO_EVAL") {
            no_eval = true;
        } else if (arg == "+REWIND") {
            rewind = true;
        } else if (arg == "+JSON") {
            use_json = true;
        }
    }
    if (!should_run) {
        std::cerr << "[Usage]: " << argv[0] << " +DEBUG_LOG" << std::endl;
        return EXIT_FAILURE;
    }

    auto vpi = set_mock();
    std::unique_ptr<hgdb::SymbolTableProvider> db =
        use_json ? setup_json_db_vpi(*vpi) : setup_sqlite_db_vpi(*vpi);

    vpi->set_argv(args);
    auto *raw_vpi = vpi.get();
    raw_vpi->set_rewind_enabled(rewind);

    auto debug = hgdb::Debugger(std::move(vpi));
    debug.initialize_db(std::move(db));

    debug.run();
    // print out so that the other side can see it
    std::cout << "INFO: START RUNNING" << std::endl;

    // evaluate the inserted breakpoint
    raw_vpi->set_time(0);
    constexpr const char *mod1_e = "top.dut.e";
    constexpr const char *mod1_f = "top.dut.f";
    using namespace std::chrono_literals;
    while (debug.is_running().load()) {
        // eval loop
        if (!no_eval) {
            debug.eval();
            auto time = raw_vpi->get_time();
            raw_vpi->set_time(time + 1);

            if (!use_json) {
                // notice that we only set dut here, so after the first breakpoint hits
                // only one will be hit later on
                raw_vpi->set_signal_value(
                    raw_vpi->vpi_handle_by_name(const_cast<char *>(mod1_e), nullptr),
                    time > 1 ? 1 : static_cast<int64_t>(time));
                // also set the array
                if (time % 2) {
                    for (auto i = 0; i < 4; i++) {
                        if (i == 3) continue;
                        auto array_name = fmt::format("top.dut.array[{0}]", i);
                        raw_vpi->set_signal_value(
                            raw_vpi->vpi_handle_by_name(const_cast<char *>(array_name.c_str()),
                                                        nullptr),
                            static_cast<int64_t>(time + 1));
                    }
                } else if (time % 3 == 2) {
                    constexpr const char *array_3 = "top.dut.array[3]";
                    raw_vpi->set_signal_value(
                        raw_vpi->vpi_handle_by_name(const_cast<char *>(array_3), nullptr),
                        static_cast<int64_t>(time + 1));
                }
                raw_vpi->set_signal_value(
                    raw_vpi->vpi_handle_by_name(const_cast<char *>(mod1_f), nullptr),
                    static_cast<int64_t>(time));
            } else {
                // json table is slightly different so we use different values
                for (auto i = 0; i < 4; i++) {
                    auto array_name = fmt::format("top.dut.array[{0}]", i);
                    raw_vpi->set_signal_value(raw_vpi->vpi_handle_by_name(
                                                  const_cast<char *>(array_name.c_str()), nullptr),
                                              static_cast<int64_t>(time + 1));
                }
                auto constexpr *addr_name = "top.dut.addr";
                raw_vpi->set_signal_value(
                    raw_vpi->vpi_handle_by_name(const_cast<char *>(addr_name), nullptr),
                    static_cast<int64_t>(time % 4));
            }

            // sleep a little to avoid high CPU load
            std::this_thread::sleep_for(10ms);
        }
    }

    std::cout << "INFO: STOP RUNNING" << std::endl;

    return EXIT_SUCCESS;
}