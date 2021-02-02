#ifndef HGDB_TOOL_ENGINE_HH
#define HGDB_TOOL_ENGINE_HH

#include <atomic>
#include <thread>

#include "vpi.hh"

namespace hgdb::replay {

class EmulationEngine {
public:
    // emulates the simulator from vcd
    // we don't take ownership of the vpi
    explicit EmulationEngine(ReplayVPIProvider* vcd);
    void run(bool blocking = true);
    void finish();

private:
    ReplayVPIProvider* vpi_;
    std::atomic<uint64_t> timestamp_ = 0;
    std::map<vpiHandle, std::optional<int64_t>> watched_values_;

    // use for non-blocking conditions
    // normally it's for testing
    std::optional<std::thread> thread_;

    // handle VPI callback
    void on_cb_added(p_cb_data cb_data);
    void on_cb_removed(const s_cb_data& cb_data);
    bool on_rewound(hgdb::AVPIProvider::rewind_data* rewind_data);

    // emulation logic
    void emulation_loop();
    void change_time(uint64_t time);
};

}  // namespace hgdb::replay

#endif  // HGDB_TOOL_ENGINE_HH
