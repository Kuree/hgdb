#ifndef HGDB_TOOL_ENGINE_HH
#define HGDB_TOOL_ENGINE_HH

#include <atomic>

#include "vpi.hh"

namespace hgdb::replay {

class EmulationEngine {
public:
    // emulates the simulator from vcd
    // we don't take ownership of the vpi
    explicit EmulationEngine(ReplayVPIProvider* vcd);
    void run();

private:
    ReplayVPIProvider* vpi_;
    std::atomic<uint64_t> timestamp_ = 0;
    std::map<vpiHandle, std::optional<int64_t>> watched_values_;

    // handle VPI callback
    void on_cb_added(p_cb_data cb_data);
    void on_cb_removed(const s_cb_data& cb_data);
    bool on_reversed(hgdb::AVPIProvider::reverse_data* reverse_data);

    // emulation logic
    void emulation_loop();
};

}  // namespace hgdb::replay

#endif  // HGDB_TOOL_ENGINE_HH
