#ifndef HGDB_TOOLS_VPI_HH
#define HGDB_TOOLS_VPI_HH

#include "rtl.hh"
#include "vcd.hh"

namespace hgdb::replay {
class ReplayVPIProvider : public hgdb::AVPIProvider {
    // notice that much of the implementation is identical to the mock vpi provider
public:
    explicit ReplayVPIProvider(std::unique_ptr<hgdb::vcd::VCDDatabase> db);
    void vpi_get_value(vpiHandle expr, p_vpi_value value_p) override;
    PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) override;
    char * vpi_get_str(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) override;
    vpiHandle vpi_scan(vpiHandle iterator) override;
    vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) override;
    PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) override;
    void vpi_get_time(vpiHandle object, p_vpi_time time_p) override;
    vpiHandle vpi_register_cb(p_cb_data cb_data_p) override;
    PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) override;
    PLI_INT32 vpi_release_handle(vpiHandle object) override;
    PLI_INT32 vpi_control(PLI_INT32 operation, ...) override;
    vpiHandle vpi_handle_by_index(vpiHandle object, PLI_INT32 index) override;
    bool vpi_reverse(reverse_data *reverse_data) override;

    void set_argv(int argc, char *argv[]);  // NOLINT

    // helper functions
    static int64_t convert_value(const std::string &raw_value);

protected:
    vpiHandle get_new_handle();

private:
    std::unique_ptr<hgdb::vcd::VCDDatabase> db_;
    uint64_t current_time_ = 0;

    // mapping from full names to handle
    std::unordered_map<std::string, vpiHandle> handle_mapping_;
    // cached module and signal ids
    std::unordered_map<vpiHandle, uint64_t> instance_id_map_;
    std::unordered_map<vpiHandle, uint64_t> signal_id_map_;

    // vpi related stuff
    char *vpi_handle_counter_ = nullptr;
    std::string str_buffer_;
    std::unordered_map<vpiHandle, std::vector<vpiHandle>> scan_map_;
    std::unordered_map<vpiHandle, uint64_t> scan_iter_;
    std::vector<std::string> argv_str_;
    std::vector<char *> argv_;
    std::unordered_map<vpiHandle, s_cb_data> callbacks_;
    constexpr static auto product = "HGDB-Replay";
    constexpr static auto version = "0.1";

    vpiHandle get_instance_handle(uint64_t instance_id);
    vpiHandle get_signal_handle(uint64_t signal_id);


};
}  // namespace hgdb::replay

#endif  // HGDB_TOOLS_VPI_HH
