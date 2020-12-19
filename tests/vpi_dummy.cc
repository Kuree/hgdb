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
PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info) { return 0; }
void vpi_get_time(vpiHandle, p_vpi_time) {}
vpiHandle vpi_register_cb(p_cb_data) { return nullptr; }
PLI_INT32 vpi_remove_cb(vpiHandle) { return 0; }
PLI_INT32 vpi_release_handle(vpiHandle) { return 0; }
PLI_INT32 vpi_control(PLI_INT32, ...) { return 0; }
}