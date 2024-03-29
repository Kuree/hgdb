#ifndef HGDB_SIM_HH
#define HGDB_SIM_HH
#include "debug.hh"

// handles simulator related functions
// C interfaces are for DPI usage
extern "C" {
__attribute__((visibility("default"))) void initialize_hgdb_runtime();
[[maybe_unused]] __attribute__((visibility("default"))) void initialize_hgdb_runtime_dpi();
}

namespace hgdb {
// cxx version is used for verilator
void initialize_hgdb_runtime_cxx(bool start_server);
[[maybe_unused]] void initialize_hgdb_runtime_cxx();
void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi);
Debugger* initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi, bool start_server);
}  // namespace hgdb

#endif  // HGDB_SIM_HH
