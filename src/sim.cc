#include "sim.hh"

#include "debug.hh"
#include "log.hh"

PLI_INT32 initialize_hgdb_debugger(p_cb_data cb_data) {
    auto *raw_debugger = cb_data->user_data;
    auto *debugger = reinterpret_cast<hgdb::Debugger *>(raw_debugger);
    debugger->run();
    return 0;
}

PLI_INT32 teardown_hgdb_debugger(p_cb_data cb_data) {
    auto *raw_debugger = cb_data->user_data;
    auto *debugger = reinterpret_cast<hgdb::Debugger *>(raw_debugger);
    debugger->stop();
    // free up the object to avoid memory leak
    delete debugger;
    return 0;
}

PLI_INT32 eval_hgdb_on_clk(p_cb_data cb_data) {
    // only if the clock value is high
    auto value = cb_data->value->value.integer;
    if (value) {
        auto *raw_debugger = cb_data->user_data;
        auto *debugger = reinterpret_cast<hgdb::Debugger *>(raw_debugger);
        debugger->eval();
    }

    return 0;
}

void initialize_hgdb_runtime() { hgdb::initialize_hgdb_runtime_vpi(nullptr); }

[[maybe_unused]] void initialize_hgdb_runtime_dpi() {
    hgdb::initialize_hgdb_runtime_vpi(nullptr, true);
}

// register the VPI call
extern "C" {
// these are system level calls. register it to the simulator
[[maybe_unused]] void (*vlog_startup_routines[])() = {initialize_hgdb_runtime, nullptr};
}

namespace hgdb {
void initialize_hgdb_runtime_cxx(bool start_server) {
    initialize_hgdb_runtime_vpi(nullptr, start_server);
}

void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi) {
    initialize_hgdb_runtime_vpi(std::move(vpi), false);
}

void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi, bool start_server) {
    // use raw pointer here since we're dealing with ancient C stuff
    Debugger *debugger;
    if (vpi) {
        debugger = new Debugger(std::move(vpi));
    } else {
        debugger = new Debugger();
    }
    char *debugger_ptr = reinterpret_cast<char *>(debugger);

    auto *rtl = debugger->rtl_client();
    vpiHandle res;

    // start the debugger at the start of the simulation
    // if start_server is set to true, start immediately
    if (start_server) {
        debugger->run();
    } else {
        res = rtl->add_call_back("initialize_hgdb", cbStartOfSimulation, initialize_hgdb_debugger,
                                 nullptr, debugger_ptr);
        if (!res) std::cerr << "ERROR: failed to register runtime initialization" << std::endl;
    }

    // teardown the debugger at the end of the simulation
    res = rtl->add_call_back("teardown_hgdb", cbEndOfSimulation, teardown_hgdb_debugger, nullptr,
                             debugger_ptr);
    if (!res) std::cerr << "ERROR: failed to register runtime initialization" << std::endl;

    // only trigger eval at the posedge clk
    auto clock_signals = debugger->get_clock_signals();
    bool r = rtl->monitor_signals(clock_signals, eval_hgdb_on_clk, debugger_ptr);
    if (!r) std::cerr << "ERROR: failed to register runtime initialization" << std::endl;
}
}  // namespace hgdb