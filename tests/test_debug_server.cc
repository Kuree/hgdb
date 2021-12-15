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
 *
 * mod dut (.*);
 *
 * endmodule
 *
 * module mod (input  logic a,
 *             input  logic clk,
 *             input  logic rst,
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
 * endmodule
 *
 */

auto setup_db_vpi(MockVPIProvider &vpi) {
    using namespace hgdb;
    // create a mock design, see comments above
    const auto *db_filename = ":memory:";
    auto db = std::make_unique<DebugDatabase>(init_debug_db(db_filename));
    db->sync_schema();
    // store
    auto variables = {"clk", "rst", "a", "b"};
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

        auto temp_variables = {"c", "c_0", "c_1", "c_2", "c_3", "d", "e"};
        for (auto const &name : temp_variables) {
            auto var_name = fmt::format("{0}.{1}", dut_instance_name, name);
            auto var_full_name = fmt::format("{0}.{1}", dut_instance_full_name, name);
            auto *handle = vpi.add_signal(dut_instance_handle, var_full_name);
            variable_handles[name] = handle;
            // this is still valid variable, just not showing up in the context/generator map
            store_variable(*db, var_id, var_name);
            variable_ids[name] = var_id;
        }

        // now we need to deal with breakpoints
        constexpr auto filename = "/tmp/test.py";
        auto base_id = (dut_id - 1) * 5;
        // assign c_0 = ~a; // a = a  en: a           -> c = ~a
        store_breakpoint(*db, 0 + base_id, dut_id, filename, 2, 0, "a");
        store_context_variable(*db, "a", 0 + base_id, variable_ids.at("a"));
        store_assignment(*db, "c", 0 + base_id, 0);
        // assign c_1 = 0;  // a = a  en: ~a          -> c = 0
        store_breakpoint(*db, 1 + base_id, dut_id, filename, 4, 0, "!a");
        store_context_variable(*db, "a", 1 + base_id, variable_ids.at("a"));
        store_assignment(*db, "c", 1 + base_id, 0);
        // assign c_2 = a? c_0: c_1; // a = a en: 1   -> if (a)
        store_breakpoint(*db, 2 + base_id, dut_id, filename, 1, 0, "1");
        store_context_variable(*db, "a", 2 + base_id, variable_ids.at("a"));
        store_assignment(*db, "c", 2 + base_id, 0);
        // assign c_3 = a; // a = a, c = c_2 en: 1     -> c = a
        store_breakpoint(*db, 3 + base_id, dut_id, filename, 5, 0, "1");
        store_context_variable(*db, "a", 3 + base_id, variable_ids.at("a"));
        store_context_variable(*db, "c", 3 + base_id, variable_ids.at("c_2"));
        store_assignment(*db, "c", 3 + base_id, 0);
        // assign d = e;
        store_breakpoint(*db, 4 + base_id, dut_id, filename, 6, 0, "", "e");
        store_context_variable(*db, "d", 4 + base_id, variable_ids.at("d"));
        store_context_variable(*db, "e", 4 + base_id, variable_ids.at("e"));

        // set values
        // we simulate the case where a is 1
        // in this case we have
        // c_0 = ~a -> 1
        // c_1 = 0
        // c_2 = 0
        // c_3 = 1
        // notice that we don't have b's value yet, so when we query we will have ERROR as the
        // result
        vpi.set_signal_value(variable_handles.at("c_0"), 1);
        vpi.set_signal_value(variable_handles.at("c_1"), 0);
        vpi.set_signal_value(variable_handles.at("c_2"), 0);
        vpi.set_signal_value(variable_handles.at("c_3"), 1);
        vpi.set_signal_value(variable_handles.at("a"), 1);

        // don't set e here but in the loop
    }

    auto db_client = std::make_unique<hgdb::DBSymbolTableProvider>(std::move(db));
    return db_client;
}

auto set_mock() {
    auto vpi = std::make_unique<MockVPIProvider>();

    return vpi;
}

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
    for (auto const &arg : args) {
        if (arg == "+DEBUG_LOG") {
            should_run = true;
        } else if (arg == "+NO_EVAL") {
            no_eval = true;
        } else if (arg == "+REWIND") {
            rewind = true;
        }
    }
    if (!should_run) {
        std::cerr << "[Usage]: " << argv[0] << " +DEBUG_LOG" << std::endl;
        return EXIT_FAILURE;
    }

    auto vpi = set_mock();
    auto db = setup_db_vpi(*vpi);

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
    using namespace std::chrono_literals;
    while (debug.is_running().load()) {
        // eval loop
        if (!no_eval) {
            debug.eval();
            auto time = raw_vpi->get_time();
            raw_vpi->set_time(time + 1);

            // notice that we only set dut here, so after the first breakpoint hits
            // only one will be hit later on
            raw_vpi->set_signal_value(
                raw_vpi->vpi_handle_by_name(const_cast<char *>(mod1_e), nullptr),
                time > 1 ? 1 : static_cast<int64_t>(time));
            // sleep a little to avoid high CPU load
            std::this_thread::sleep_for(10ms);
        }
    }

    std::cout << "INFO: STOP RUNNING" << std::endl;

    return EXIT_SUCCESS;
}