#ifndef HGDB_RTL_HH
#define HGDB_RTL_HH

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
    virtual PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) = 0;
    virtual void vpi_get_time(vpiHandle object, p_vpi_time time_p) = 0;
    virtual vpiHandle vpi_register_cb(p_cb_data cb_data_p) = 0;
    virtual PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) = 0;
    virtual PLI_INT32 vpi_release_handle(vpiHandle object) = 0;
    virtual PLI_INT32 vpi_control(PLI_INT32 operation, ...) = 0;
    virtual ~AVPIProvider() = default;
};

class VPIProvider : public AVPIProvider {
    void vpi_get_value(vpiHandle expr, p_vpi_value value_p) override;
    PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) override;
    vpiHandle vpi_scan(vpiHandle iterator) override;
    char *vpi_get_str(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) override;
    PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) override;
    void vpi_get_time(vpiHandle object, p_vpi_time time_p) override;
    vpiHandle vpi_register_cb(p_cb_data cb_data_p) override;
    PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) override;
    PLI_INT32 vpi_release_handle(vpiHandle object) override;
    PLI_INT32 vpi_control(PLI_INT32 operation, ...) override;

private:
    std::mutex vpi_lock_;
};

class RTLSimulatorClient {
public:
    explicit RTLSimulatorClient(std::unique_ptr<AVPIProvider> vpi);
    explicit RTLSimulatorClient(const std::vector<std::string> &instance_names);
    RTLSimulatorClient(const std::vector<std::string> &instance_names,
                       std::unique_ptr<AVPIProvider> vpi);
    void initialize(const std::vector<std::string> &instance_names,
                    std::unique_ptr<AVPIProvider> vpi);
    void initialize_instance_mapping(const std::vector<std::string> &instance_names);
    void initialize_vpi(std::unique_ptr<AVPIProvider> vpi);
    vpiHandle get_handle(const std::string &name);
    std::optional<int64_t> get_value(const std::string &name);
    std::optional<int64_t> get_value(vpiHandle handle);
    using ModuleSignals = std::unordered_map<std::string, vpiHandle>;
    ModuleSignals get_module_signals(const std::string &name);
    [[nodiscard]] std::string get_full_name(const std::string &name) const;
    [[nodiscard]] const std::vector<std::string> &get_argv();
    [[nodiscard]] const std::string &get_simulator_name();
    [[nodiscard]] uint64_t get_simulation_time() const;
    // can't use std::function due to C interface
    vpiHandle add_call_back(const std::string &cb_name, int cb_type, int(cb_func)(p_cb_data),
                            vpiHandle obj = nullptr, void *user_data = nullptr);
    void remove_call_back(const std::string &cb_name);
    void remove_call_back(vpiHandle cb_handle);
    enum class finish_value { nothing = 0, time_location = 1, all = 2 };
    void finish_sim(finish_value value = finish_value::nothing);
    void stop_sim(finish_value value = finish_value::nothing);

    // expose raw vpi client if necessary
    AVPIProvider &vpi() { return *vpi_; }

    // indicate if the simulator is verilator
    bool is_verilator();

    // destructor to avoid memory leak in the simulator
    ~RTLSimulatorClient();

private:
    std::unordered_map<std::string, vpiHandle> handle_map_;
    // it is a map just in case there are separated tops being generated
    // in this case, each top needs to get mapped to a different hierarchy
    std::unordered_map<std::string, std::string> hierarchy_name_prefix_map_;
    // VPI provider
    std::unique_ptr<AVPIProvider> vpi_;
    uint32_t vpi_net_target_ = vpiNet;
    // callbacks
    std::unordered_map<std::string, vpiHandle> cb_handles_;

    // cached module signals
    // this is to avoid to loop through instances repeatedly
    std::unordered_map<std::string, ModuleSignals> module_signals_cache_;

    // simulator info
    struct SimulatorInfo {
        std::string name;
        std::vector<std::string> args;
    };
    std::optional<SimulatorInfo> sim_info_;

    // only used for verilator
    std::optional<bool> is_verilator_;

    static std::pair<std::string, std::string> get_path(const std::string &name);
    void compute_hierarchy_name_prefix(std::unordered_set<std::string> &top_names);
    void get_simulator_info();
    void compute_verilator_name_prefix(std::unordered_set<std::string> &top_names);
};
}  // namespace hgdb

#endif  // HGDB_RTL_HH
