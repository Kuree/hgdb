#ifndef HGDB_TOOL_ENGINE_HH
#define HGDB_TOOL_ENGINE_HH

#include "vpi.hh"

namespace hgdb::replay {

class EmulationEngine {
    // emulates the simulator from vcd
    // we take ownership of the vpi
    explicit EmulationEngine(std::unique_ptr<ReplayVPIProvider> vcd);
    void start();

private:
    std::unique_ptr<ReplayVPIProvider> vcd_;
    uint64_t timestamp_ = 0;

    // handle VPI callback
    void on_cb_added(p_cb_data cb_data);
    void on_cb_removed(const s_cb_data &cb_data);
    void on_reversed(hgdb::AVPIProvider::reverse_data* reverse_data);
};

}

#endif  // HGDB_TOOL_ENGINE_HH
