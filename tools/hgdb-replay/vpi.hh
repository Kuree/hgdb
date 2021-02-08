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
    char *vpi_get_str(PLI_INT32 property, vpiHandle object) override;
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
    bool vpi_rewind(rewind_data *rewind_data) override;

    // interaction with outside world
    void set_argv(int argc, char *argv[]);  // NOLINT
    void set_on_cb_added(const std::function<void(p_cb_data)> &on_cb_added);
    void set_on_cb_removed(const std::function<void(const s_cb_data &)> &on_cb_removed);
    void set_on_reversed(const std::function<bool(rewind_data *)> &on_reversed);
    void set_timestamp(uint64_t time) { current_time_ = time; }
    hgdb::vcd::VCDDatabase &db() { return *db_; }
    bool is_valid_handle(vpiHandle handle) const;
    void trigger_cb(uint32_t reason) { trigger_cb(reason, nullptr, 0); }
    void trigger_cb(uint32_t reason, vpiHandle obj, int64_t value);
    std::optional<uint64_t> get_signal_id(vpiHandle handle);
    void set_is_callback_eval(bool value) { is_callback_eval_ = value; }
    // used to override values in get_value
    void add_overridden_value(vpiHandle handle, int64_t value);
    void clear_overridden_values();

    // helper functions
    static int64_t convert_value(const std::string &raw_value);
    static std::string convert_str_value(const std::string &raw_value);

protected:
    vpiHandle get_new_handle();

private:
    std::unique_ptr<hgdb::vcd::VCDDatabase> db_;
    uint64_t current_time_ = 0;
    // if it's in callback_eval, when we do get time we have to + 1 since we are getting
    // stabilized values
    bool is_callback_eval_ = false;
    std::unordered_map<vpiHandle, int64_t> overridden_values_;

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

    vpiHandle get_instance_handle(uint64_t instance_id) const;
    vpiHandle get_signal_handle(uint64_t signal_id) const;

    // callbacks
    std::optional<std::function<void(p_cb_data)>> on_cb_added_;
    std::optional<std::function<void(const s_cb_data &)>> on_cb_removed_;
    std::optional<std::function<bool(rewind_data *)>> on_rewound_;
};
}  // namespace hgdb::replay

#endif  // HGDB_TOOLS_VPI_HH
