#include "sim.hh"

#include "debug.hh"
#include "log.hh"

constexpr auto HGDB_ASSERT_FAIL = "$hgdb_assert_fail";

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

PLI_INT32 eval_hgdb(p_cb_data cb_data) {
    auto *raw_debugger = cb_data->user_data;
    auto *debugger = reinterpret_cast<hgdb::Debugger *>(raw_debugger);
    debugger->eval();
    // Verilator seems to only need to schedule once per simulation?
    // I don't think the LRM actually specifies this
    return 0;
}

void initialize_hgdb_runtime() { hgdb::initialize_hgdb_runtime_vpi(nullptr); }

[[maybe_unused]] void initialize_hgdb_runtime_dpi() {
    hgdb::initialize_hgdb_runtime_vpi(nullptr, true);
}

int handle_assert(char *user_data) {
    auto *debugger = reinterpret_cast<hgdb::Debugger *>(user_data);
    debugger->handle_assert();
    return 0;
}

void register_tf(hgdb::RTLSimulatorClient *rtl, hgdb::Debugger *debugger) {
    // notice that verilator does not support system task
    if (rtl->is_verilator()) return;
    auto *res = rtl->register_tf(HGDB_ASSERT_FAIL, &handle_assert, debugger);
    if (!res) {
        std::cerr << "ERROR: failed to register system function " << HGDB_ASSERT_FAIL << std::endl;
    }
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

[[maybe_unused]] void initialize_hgdb_runtime_cxx() { initialize_hgdb_runtime_cxx(true); }

void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi) {
    initialize_hgdb_runtime_vpi(std::move(vpi), false);
}

Debugger *initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi, bool start_server) {
    // some hacks to detect VCS
    // when it's producing the simv executable, VCS will run this function
    // then when you run ./simv, it runs this function again
    // why would VCS do this?
    // using Q-2020.03-SP2_Full64 at the time of testing
    {
        t_vpi_vlog_info info{};
        // luckily during that stage, vpi_get_vlog_info will report error
        // we will use that as an indication of VCS compilation stage
        if (!start_server && !vpi_get_vlog_info(&info) && !vpi) {
            // we are inside VCS command
            // still need to register the TF, using the system VPI since it's VCS
            RTLSimulatorClient rtl(std::make_shared<hgdb::VPIProvider>());
            // no need to create debugger
            register_tf(&rtl, nullptr);
            return nullptr;
        }
    }

    // use raw pointer here since we're dealing with ancient C stuff
    Debugger *debugger;
    if (vpi) {
        debugger = new Debugger(std::move(vpi));
    } else {
        debugger = new Debugger();
    }
    char *debugger_ptr = reinterpret_cast<char *>(debugger);

    auto *rtl = debugger->rtl_clients()[0];
    vpiHandle res;

    // register system tf
    register_tf(rtl, debugger);

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
    if (!res) std::cerr << "ERROR: failed to register runtime tear down" << std::endl;

    // special case for Verilator
    // cbValueChange on clock is tricky in Verilator because once you call eval, the states are
    // already updated
    if (rtl->is_verilator()) {
        res = rtl->add_call_back("eval_hgdb", cbNextSimTime, eval_hgdb, nullptr,
                                 reinterpret_cast<char *>(debugger));
        if (!res) std::cerr << "ERROR: failed to register runtime initialization" << std::endl;
    }
    return debugger;
}
}  // namespace hgdb