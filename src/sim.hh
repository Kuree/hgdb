#ifndef HGDB_SIM_HH
#define HGDB_SIM_HH
#include "rtl.hh"

// handles simulator related functions
// C interfaces are for VPI/DPI usage
extern "C" {
__attribute__((visibility("default"))) void initialize_hgdb_runtime();
}

namespace hgdb {
void initialize_hgdb_runtime_vpi(std::unique_ptr<AVPIProvider> vpi);
}

#endif  // HGDB_SIM_HH
