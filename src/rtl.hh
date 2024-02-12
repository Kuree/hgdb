#ifndef HGDB_RTL_HH
#define HGDB_RTL_HH

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vpi_user.h"

namespace hgdb {

// abstract class to handle VPI implementation
// needed for mock tests. In real world, always use vendor provided implementation
class AVPIProvider {
public:
    virtual void vpi_get_value(vpiHandle expr, p_vpi_value value_p) = 0;
    virtual PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) = 0;
    virtual vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) = 0;
    virtual vpiHandle vpi_scan(vpiHandle iterator) = 0;
    virtual char *vpi_get_str(PLI_INT32 property, vpiHandle object) = 0;
    virtual vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) = 0;
    virtual vpiHandle vpi_handle_by_index(vpiHandle object, PLI_INT32 index) = 0;
    virtual PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) = 0;
    virtual void vpi_get_time(vpiHandle object, p_vpi_time time_p) = 0;
    virtual vpiHandle vpi_register_cb(p_cb_data cb_data_p) = 0;
    virtual PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) = 0;
    virtual PLI_INT32 vpi_release_handle(vpiHandle object) = 0;
    virtual PLI_INT32 vpi_control(PLI_INT32 operation, ...) = 0;
    virtual vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p,
                                    PLI_INT32 flags) = 0;
    // these two below are used to implement assertions
    virtual vpiHandle vpi_register_systf(p_vpi_systf_data data) = 0;
    virtual vpiHandle vpi_handle(int type, vpiHandle scope) = 0;
    virtual ~AVPIProvider() = default;

    // extended vpi controls, not present in the spec
    struct rewind_data {
        // threshold time (the reversed timestamp has to be strictly < the given time)
        uint64_t time;
        // clock_handle
        std::vector<vpiHandle> clock_signals;
    };
    virtual bool vpi_rewind(rewind_data *reverse_data) { return false; }

    void set_use_lock_getting_value(bool value) { use_lock_getting_value_ = value; }

    // used to indicate whether the underlying simulator supports vpiDefName
    virtual bool has_defname() = 0;

protected:
    bool use_lock_getting_value_ = true;
};

class VPIProvider : public AVPIProvider {
    void vpi_get_value(vpiHandle expr, p_vpi_value value_p) override;
    PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) override;
    vpiHandle vpi_scan(vpiHandle iterator) override;
    char *vpi_get_str(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) override;
    vpiHandle vpi_handle_by_index(vpiHandle object, PLI_INT32 index) override;
    PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) override;
    void vpi_get_time(vpiHandle object, p_vpi_time time_p) override;
    vpiHandle vpi_register_cb(p_cb_data cb_data_p) override;
    PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) override;
    PLI_INT32 vpi_release_handle(vpiHandle object) override;
    PLI_INT32 vpi_control(PLI_INT32 operation, ...) override;
    vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p,
                            PLI_INT32 flags) override;
    vpiHandle vpi_register_systf(p_vpi_systf_data data) override;
    vpiHandle vpi_handle(int type, vpiHandle scope) override;

    bool has_defname() override;

private:
    std::mutex vpi_lock_;
};

class RTLSimulatorClient {
public:
    explicit RTLSimulatorClient(std::shared_ptr<AVPIProvider> vpi);

    using IPMapping = std::pair<std::string, std::string>;
    std::vector<RTLSimulatorClient::IPMapping> compute_instance_mapping(
        const std::vector<std::string> &instance_names, bool use_definition);
    void initialize_vpi(std::shared_ptr<AVPIProvider> vpi);
    vpiHandle get_handle(const std::string &name);
    vpiHandle get_handle(const std::vector<std::string> &tokens);
    bool is_valid_signal(const std::string &name);
    std::optional<int64_t> get_value(const std::string &name);
    std::optional<int64_t> get_value(vpiHandle handle, bool signal = true);
    std::optional<uint32_t> get_signal_width(vpiHandle handle);
    std::optional<std::string> get_str_value(const std::string &name);
    std::optional<std::string> get_str_value(vpiHandle handle, bool is_signal = true);
    bool set_value(vpiHandle handle, int64_t value);
    bool set_value(const std::string &name, int64_t value);
    using ModuleSignals = std::unordered_map<std::string, vpiHandle>;
    ModuleSignals get_module_signals(const std::string &name);
    [[nodiscard]] std::string get_full_name(const std::string &name) const;
    [[nodiscard]] std::string get_full_name(vpiHandle handle);
    [[nodiscard]] bool is_absolute_path(const std::string &name) const;
    [[nodiscard]] const std::vector<std::string> &get_argv() const;
    [[nodiscard]] const std::string &get_simulator_name() const;
    [[nodiscard]] const std::string &get_simulator_version() const;
    [[nodiscard]] uint64_t get_simulation_time() const;
    // can't use std::function due to C interface
    vpiHandle add_call_back(const std::string &cb_name, int cb_type, int(cb_func)(p_cb_data),
                            vpiHandle obj = nullptr, void *user_data = nullptr);
    void remove_call_back(const std::string &cb_name);
    vpiHandle register_tf(const std::string &name, int(tf_func)(char *), void *user_data = nullptr);
    enum class finish_value { nothing = 0, time_location = 1, all = 2 };
    void finish_sim(finish_value value = finish_value::nothing);
    void stop_sim(finish_value value = finish_value::nothing);
    // deal with struct
    std::vector<std::pair<std::string, std::string>> resolve_rtl_variable(
        const std::string &front_name, std::string rtl_name);

    // expose raw vpi client if necessary
    std::shared_ptr<AVPIProvider> vpi() { return vpi_; }

    // indicate if the simulator is verilator
    [[nodiscard]] bool is_verilator() const { return is_verilator_; }
    [[nodiscard]] bool is_vcs() const { return is_vcs_; }
    [[maybe_unused]] [[nodiscard]] bool is_xcelium() const { return is_xcelium_; }
    [[nodiscard]] bool is_mock() const { return is_mock_; }

    // search for clock signals
    [[nodiscard]] std::vector<std::string> get_clocks_from_design();
    // add monitors on signals
    [[nodiscard]] bool monitor_signals(const std::vector<std::string> &signals,
                                       int(cb_func)(p_cb_data), void *user_data);

    // callback related
    [[nodiscard]] std::unordered_set<std::string> callback_names();

    // reverse simulation supported
    // given a list of clock handles, reverse the execution
    [[maybe_unused]] bool reverse_last_posedge(const std::vector<vpiHandle> &clk_handles);

    [[nodiscard]] bool rewind(uint64_t time, const std::vector<vpiHandle> &clk_handles);

    // if the client uses some custom vpiHandle allocator, they need to set this to avoid
    // conflicts
    void set_vpi_allocator(const std::function<vpiHandle()> &func);

    // destructor to avoid memory leak in the simulator
    ~RTLSimulatorClient();

    // used for compute potential clock signals if user doesn't provide proper annotation
    static constexpr std::array clock_names_{"clk", "clock", "clk_in", "clock_in", "CLK", "CLOCK"};
    static constexpr std::string_view root_name = "$root.";

    // inform user about our mapping
    void set_mapping(const std::string &top, const std::string &prefix);
    [[nodiscard]] auto const &get_mapping() const { return hierarchy_name_prefix_map_; }

    struct AssertInfo {
        std::string full_name;
        std::string filename;
        uint32_t line = 0;
        uint32_t column = 0;
    };

    std::optional<RTLSimulatorClient::AssertInfo> get_assert_info();

private:
    std::unordered_map<std::string, vpiHandle> handle_map_;
    std::mutex handle_map_lock_;
    std::pair<std::string, std::string> hierarchy_name_prefix_map_;
    // VPI provider
    std::shared_ptr<AVPIProvider> vpi_;
    uint32_t vpi_net_target_ = vpiNet;
    // callbacks
    std::unordered_map<std::string, vpiHandle> cb_handles_;
    std::mutex cb_handles_lock_;
    std::unordered_map<vpiHandle, PLI_INT32> cached_vpi_types_;
    std::mutex cached_vpi_types_lock_;
    std::unordered_map<vpiHandle, uint32_t> cached_vpi_size_;
    std::mutex cached_vpi_size_lock_;

    // cached module signals
    // this is to avoid looping through instances repeatedly
    std::unordered_map<std::string, ModuleSignals> module_signals_cache_;

    // notice that to my best knowledge, there is no command VPI routine that shared by all
    // simulator vendors that deal with slices. As a result, we need to fake vpiHandles to deal
    // with slices
    std::unordered_map<vpiHandle, std::tuple<vpiHandle, uint32_t, uint32_t>> mock_slice_handles_;
    vpiHandle mock_slice_handle_counter_ = nullptr;
    std::optional<std::function<vpiHandle()>> vpi_allocator_;

    // simulator info
    struct SimulatorInfo {
        std::string name;
        std::string version;
        std::vector<std::string> args;
    };
    SimulatorInfo sim_info_;

    // only used for verilator
    bool is_verilator_ = false;
    bool is_xcelium_ = false;
    bool is_vcs_ = false;
    bool is_iverilog_ = false;
    bool is_mock_ = false;

    static std::pair<std::string, std::string> get_path(const std::string &name);
    std::vector<IPMapping> compute_hierarchy_name_prefix(
        const std::unordered_set<std::string> &top_names);
    std::vector<IPMapping> compute_hierarchy_name_prefix(const std::vector<std::string> &instances);
    void set_simulator_info();

    // brute-force to deal with verilator array indexing stuff
    using StringIterator = typename std::vector<std::string>::const_iterator;
    vpiHandle access_arrays(StringIterator begin, StringIterator end, vpiHandle var_handle);

    // cached helper methods
    PLI_INT32 get_vpi_type(vpiHandle handle);
    uint32_t get_vpi_size(vpiHandle handle);

    // other helper functions
    void remove_call_back(vpiHandle cb_handle);
    std::optional<std::function<std::unordered_map<std::string, std::string>(
        const std::unordered_set<std::string> &)>>
        custom_hierarchy_func_;
    vpiHandle add_mock_slice_vpi(vpiHandle parent, const std::string &slice);
    vpiHandle get_handle_raw(const std::string &handle_name);
};
}  // namespace hgdb

#endif  // HGDB_RTL_HH
