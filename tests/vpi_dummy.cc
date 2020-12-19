#include "vpi_user.h"

// dummy VPI files to allow tests that uses VPI to compile properly
// do not include this file for the actual library!

extern "C" {
void vpi_get_value(vpiHandle, p_vpi_value) {}
PLI_INT32 vpi_get(PLI_INT32, vpiHandle) { return 0; }
vpiHandle vpi_iterate(PLI_INT32, vpiHandle) { return nullptr; }
vpiHandle vpi_scan(vpiHandle) { return nullptr; }
char *vpi_get_str(PLI_INT32, vpiHandle) { return nullptr; }
vpiHandle vpi_handle_by_name(char *, vpiHandle) { return nullptr; }
}