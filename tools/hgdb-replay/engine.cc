#include "engine.hh"

namespace hgdb::replay {

using reverse_data = hgdb::AVPIProvider::reverse_data;

EmulationEngine::EmulationEngine(std::unique_ptr<ReplayVPIProvider> vcd) : vcd_(std::move(vcd)) {
    // set callbacks
    vcd_->set_on_cb_added([this](p_cb_data cb_data) { on_cb_added(cb_data); });
    vcd_->set_on_cb_removed([this](const s_cb_data& cb_data) { on_cb_removed(cb_data); });
    vcd_->set_on_reversed([this](reverse_data* reverse_data) { on_reversed(reverse_data); });

    // set time
    vcd_->set_timestamp(timestamp_);
}

void EmulationEngine::start() {
    
}


void EmulationEngine::on_cb_added(p_cb_data cb_data) {

}

void EmulationEngine::on_cb_removed(const s_cb_data& cb_data) {

}

void EmulationEngine::on_reversed(hgdb::AVPIProvider::reverse_data* reverse_data) {

}

}  // namespace hgdb::replay
