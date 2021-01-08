#ifndef HGDB_SIM_HH
#define HGDB_SIM_HH
#include "rtl.hh"

// handles simulator related functions
// C interfaces are for DPI usage
extern "C" {
__attribute__((visibility("default"))) void initialize_hgdb_runtime();
[[maybe_unused]] __attribute__((visibility("default"))) void initialize_hgdb_runtime_dpi();
}

namespace hgdb {
// cxx version is used for verilator
void initialize_hgdb_runtime_cxx(bool start_server);
void initialize_hgdb_runtime_cxx() { initialize_hgdb_runtime_cxx(true); }
void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi);
void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi, bool start_server);
}

#endif  // HGDB_SIM_HH
