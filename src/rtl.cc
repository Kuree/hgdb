#include "rtl.hh"

#include <fmt/format.h>

#include <cstdarg>
#include <queue>
#include <unordered_set>

#include "util.hh"

namespace hgdb {

void VPIProvider::vpi_get_value(vpiHandle expr, p_vpi_value value_p) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_get_value(expr, value_p);
}

PLI_INT32 VPIProvider::vpi_get(PLI_INT32 property, vpiHandle object) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_get(property, object);
}

vpiHandle VPIProvider::vpi_iterate(PLI_INT32 type, vpiHandle refHandle) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_iterate(type, refHandle);
}

vpiHandle VPIProvider::vpi_scan(vpiHandle iterator) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_scan(iterator);
}

char *VPIProvider::vpi_get_str(PLI_INT32 property, vpiHandle object) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_get_str(property, object);
}

vpiHandle VPIProvider::vpi_handle_by_name(char *name, vpiHandle scope) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_handle_by_name(name, scope);
}

vpiHandle VPIProvider::vpi_handle_by_index(vpiHandle object, PLI_INT32 index) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_handle_by_index(object, index);
}

PLI_INT32 VPIProvider::vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_get_vlog_info(vlog_info_p);
}

void VPIProvider::vpi_get_time(vpiHandle object, p_vpi_time time_p) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_get_time(object, time_p);
}

vpiHandle VPIProvider::vpi_register_cb(p_cb_data cb_data_p) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_register_cb(cb_data_p);
}

PLI_INT32 VPIProvider::vpi_remove_cb(vpiHandle cb_obj) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_remove_cb(cb_obj);
}

PLI_INT32 VPIProvider::vpi_release_handle(vpiHandle object) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_release_handle(object);
}

PLI_INT32 VPIProvider::vpi_control(PLI_INT32 operation, ...) {
    std::lock_guard guard(vpi_lock_);
    std::va_list args;
    va_start(args, operation);
    auto result = ::vpi_control(operation, args);
    va_end(args);
    return result;
}

RTLSimulatorClient::RTLSimulatorClient(std::unique_ptr<AVPIProvider> vpi) {
    initialize_vpi(std::move(vpi));
}

RTLSimulatorClient::RTLSimulatorClient(const std::vector<std::string> &instance_names)
    : RTLSimulatorClient(instance_names, nullptr) {}

RTLSimulatorClient::RTLSimulatorClient(const std::vector<std::string> &instance_names,
                                       std::unique_ptr<AVPIProvider> vpi) {
    initialize(instance_names, std::move(vpi));
}

void RTLSimulatorClient::initialize(const std::vector<std::string> &instance_names,
                                    std::unique_ptr<AVPIProvider> vpi) {
    initialize_vpi(std::move(vpi));
    initialize_instance_mapping(instance_names);
}

void RTLSimulatorClient::initialize_instance_mapping(
    const std::vector<std::string> &instance_names) {
    std::unordered_set<std::string> top_names;
    for (auto const &name : instance_names) {
        auto top = get_path(name).first;
        top_names.emplace(top);
    }
    // compute the naming map
    compute_hierarchy_name_prefix(top_names);
}

void RTLSimulatorClient::initialize_vpi(std::unique_ptr<AVPIProvider> vpi) {
    // if vpi provider is null, we use the system default one
    if (!vpi) {
        vpi_ = std::make_unique<VPIProvider>();
    } else {
        // we take the ownership
        vpi_ = std::move(vpi);
    }
    // set simulator information
    set_simulator_info();

    // compute the vpiNet target. this is a special case for Verilator
    vpi_net_target_ = is_verilator() ? vpiReg : vpiNet;
}

vpiHandle RTLSimulatorClient::get_handle(const std::string &name) {
    auto full_name = get_full_name(name);
    // if we already queried this handle before
    std::lock_guard guard(handle_map_lock_);
    if (handle_map_.find(full_name) != handle_map_.end()) [[likely]] {
        return handle_map_.at(full_name);
    } else {
        // need to query via VPI
        auto *handle = const_cast<char *>(full_name.c_str());
        auto *ptr = vpi_->vpi_handle_by_name(handle, nullptr);
        if (ptr) [[likely]] {
            // if we actually found the handle, need to store it
            handle_map_.emplace(full_name, ptr);
        } else {
            // full back to brute-force resolving names. usually we have to
            // deal with verilator. remove []
            auto tokens = util::get_tokens(full_name, ".[]");
            ptr = get_handle(tokens);
            if (ptr) handle_map_.emplace(name, ptr);
        }
        return ptr;
    }
}

vpiHandle RTLSimulatorClient::get_handle(const std::vector<std::string> &tokens) {
    if (tokens.empty()) [[unlikely]] {
        return nullptr;
    } else {
        // notice that this will be called inside the normal get_handle
        // we we can assume that handle_map_ is well protected
        // strip off the trailing tokens until it becomes a variable
        for (auto i = tokens.size() - 1; i > 0; i--) {
            auto pos = tokens.begin() + i;
            auto handle_name = util::join(tokens.begin(), pos, ".");
            vpiHandle ptr;
            if (handle_map_.find(handle_name) != handle_map_.end()) {
                ptr = handle_map_.at(handle_name);
            } else {
                auto *handle_name_ptr = const_cast<char *>(handle_name.c_str());
                ptr = vpi_->vpi_handle_by_name(handle_name_ptr, nullptr);
                if (ptr) {
                    handle_map_.emplace(handle_name, ptr);
                }
            }
            if (ptr) {
                auto type = get_vpi_type(ptr);
                if (type != vpiModule) {
                    // best effort
                    // notice that we only support array indexing, since struct
                    // access should handled by simulator properly
                    return access_arrays(pos, tokens.end(), ptr);
                }
            }
        }
        return nullptr;
    }
}

bool RTLSimulatorClient::is_valid_signal(const std::string &name) {
    auto full_name = get_full_name(name);
    auto *handle = get_handle(full_name);
    if (!handle) return false;
    auto type = get_vpi_type(handle);
    return type == vpiReg || type == vpiNet || type == vpiRegArray || type == vpiRegBit ||
           type == vpiNetArray || type == vpiNetBit;
}

vpiHandle RTLSimulatorClient::access_arrays(StringIterator begin, StringIterator end,
                                            vpiHandle var_handle) {
    auto it = begin;
    while (it != end) {
        auto idx = *it;
        if (!std::all_of(idx.begin(), idx.end(), ::isdigit)) {
            return nullptr;
        }
        auto index_value = std::stoll(idx);
        var_handle = vpi_->vpi_handle_by_index(var_handle, index_value);
        if (!var_handle) return nullptr;
        it++;
    }
    return var_handle;
}

std::optional<int64_t> RTLSimulatorClient::get_value(vpiHandle handle) {
    if (!handle) [[unlikely]] {
        return std::nullopt;
    }
    auto type = get_vpi_type(handle);
    if (type == vpiModule) [[unlikely]] {
        return std::nullopt;
    }
    s_vpi_value v;
    v.format = vpiIntVal;
    vpi_->vpi_get_value(handle, &v);
    int64_t result = v.value.integer;
    return result;
}

std::optional<int64_t> RTLSimulatorClient::get_value(const std::string &name) {
    auto *handle = get_handle(name);
    return get_value(handle);
}

std::unordered_map<std::string, vpiHandle> RTLSimulatorClient::get_module_signals(
    const std::string &name) {
    if (module_signals_cache_.find(name) != module_signals_cache_.end()) {
        return module_signals_cache_.at(name);
    }
    auto *module_handle = get_handle(name);
    if (!module_handle) return {};
    // need to make sure it is module type
    auto module_handle_type = get_vpi_type(module_handle);
    if (module_handle_type != vpiModule) return {};

    std::unordered_map<std::string, vpiHandle> result;
    // get all net from that particular module
    auto *net_iter = vpi_->vpi_iterate(vpi_net_target_, module_handle);
    if (!net_iter) return {};
    vpiHandle net_handle;
    while ((net_handle = vpi_->vpi_scan(net_iter)) != nullptr) {
        char *name_raw = vpi_->vpi_get_str(vpiName, net_handle);
        std::string n = name_raw;
        result.emplace(n, net_handle);
    }

    // store the cache
    module_signals_cache_.emplace(name, result);
    return result;
}

std::string RTLSimulatorClient::get_full_name(const std::string &name) const {
    auto const [top, path] = get_path(name);
    if (hierarchy_name_prefix_map_.find(top) == hierarchy_name_prefix_map_.end()) {
        // we haven't seen this top. it has to be an error since we requires top name
        // setup in the constructor. return the original name
        return name;
    } else {
        auto prefix = hierarchy_name_prefix_map_.at(top);
        if (path.empty())
            return prefix.substr(0, prefix.size() - 1);
        else
            return prefix + path;
    }
}

const std::vector<std::string> &RTLSimulatorClient::get_argv() const { return sim_info_.args; }

const std::string &RTLSimulatorClient::get_simulator_name() const { return sim_info_.name; }

const std::string &RTLSimulatorClient::get_simulator_version() const { return sim_info_.version; }

uint64_t RTLSimulatorClient::get_simulation_time() const {
    // we use sim time
    s_vpi_time current_time{};
    current_time.type = vpiSimTime;
    current_time.real = 0;
    current_time.high = 0;
    current_time.low = 0;
    vpi_->vpi_get_time(nullptr, &current_time);
    uint64_t high = current_time.high;
    uint64_t low = current_time.low;
    return high << 32u | low;
}

vpiHandle RTLSimulatorClient::add_call_back(const std::string &cb_name, int cb_type,
                                            int (*cb_func)(p_cb_data), vpiHandle obj,  // NOLINT
                                            void *user_data) {
    std::lock_guard guard(cb_handles_lock_);
    if (cb_handles_.find(cb_name) != cb_handles_.end()) [[unlikely]] {
        return cb_handles_.at(cb_name);
    }
    static s_vpi_time time{vpiSimTime};
    static s_vpi_value value{vpiIntVal};
    s_cb_data cb_data{.reason = cb_type,
                      .cb_rtn = cb_func,
                      .obj = obj,
                      .time = &time,
                      .value = &value,
                      .user_data = reinterpret_cast<char *>(user_data)};
    auto *handle = vpi_->vpi_register_cb(&cb_data);
    if (!handle) {
        // need to free the old one to avoid memory leak
        if (cb_handles_.find(cb_name) != cb_handles_.end()) {
            auto *old_handle = cb_handles_.at(cb_name);
            vpi_->vpi_release_handle(old_handle);
            cb_handles_.erase(cb_name);
        }
        cb_handles_.emplace(cb_name, handle);
    }

    return handle;
}

void RTLSimulatorClient::remove_call_back(const std::string &cb_name) {
    std::lock_guard guard(cb_handles_lock_);
    if (cb_handles_.find(cb_name) != cb_handles_.end()) {
        auto *handle = cb_handles_.at(cb_name);
        remove_call_back(handle);
    }
}

void RTLSimulatorClient::remove_call_back(vpiHandle cb_handle) {
    // notice that this is not locked!
    // remove it from the cb_handles if any
    for (auto const &iter : cb_handles_) {
        if (iter.second == cb_handle) {
            cb_handles_.erase(iter.first);
            break;
        }
    }
    vpi_->vpi_remove_cb(cb_handle);
    vpi_->vpi_release_handle(cb_handle);
}

void RTLSimulatorClient::stop_sim(finish_value value) {
    vpi_->vpi_control(vpiStop, static_cast<int>(value));
}

void RTLSimulatorClient::finish_sim(finish_value value) {
    vpi_->vpi_control(vpiFinish, static_cast<int>(value));
}

std::pair<std::string, std::string> RTLSimulatorClient::get_path(const std::string &name) {
    auto pos = name.find_first_of('.');
    if (pos == std::string::npos) {
        return {name, ""};
    } else {
        auto top = name.substr(0, pos);
        auto n = name.substr(pos + 1);
        return {top, n};
    }
}

void RTLSimulatorClient::compute_hierarchy_name_prefix(std::unordered_set<std::string> &top_names) {
    // Verilator doesn't support vpiDefName
    if (is_verilator()) {
        compute_verilator_name_prefix(top_names);
        return;
    }
    // we do a BFS search from the top;
    std::queue<vpiHandle> handle_queues;
    handle_queues.emplace(nullptr);
    while ((!handle_queues.empty()) && !top_names.empty()) {
        // scan through the design hierarchy
        auto *mod_handle = handle_queues.front();
        handle_queues.pop();
        auto *handle_iter = vpi_->vpi_iterate(vpiModule, mod_handle);
        if (!handle_iter) continue;
        vpiHandle child_handle;
        while ((child_handle = vpi_->vpi_scan(handle_iter)) != nullptr) {
            std::string def_name = vpi_->vpi_get_str(vpiDefName, child_handle);
            if (top_names.find(def_name) != top_names.end()) {
                // we found a match
                std::string hierarchy_name = vpi_->vpi_get_str(vpiFullName, child_handle);
                // adding . at the end
                hierarchy_name = fmt::format("{0}.", hierarchy_name);
                // add it to the mapping
                hierarchy_name_prefix_map_.emplace(def_name, hierarchy_name);
                top_names.erase(def_name);
            }
            handle_queues.emplace(child_handle);
        }
    }
}

void RTLSimulatorClient::set_simulator_info() {
    t_vpi_vlog_info info{};
    if (vpi_->vpi_get_vlog_info(&info)) {
        SimulatorInfo sim_info;
        sim_info.name = info.product;
        sim_info.version = info.version;
        sim_info.args.reserve(info.argc);
        for (int i = 0; i < info.argc; i++) {
            std::string argv = info.argv[i];
            sim_info.args.emplace_back(argv);
        }
        sim_info_ = sim_info;
    } else {
        // can't get simulator info
        sim_info_ = SimulatorInfo{};
    }

    is_verilator_ = sim_info_.name == "Verilator";
    is_xcelium_ = sim_info_.name.find("xmsim") != std::string::npos;
    // don't ask me why there is an extra space at the end
    // "Chronologic Simulation VCS Release "
    // to be safe all the matching is done by string find, instead of exact match, except
    // verilator
    is_vcs_ = sim_info_.name.find("VCS") != std::string::npos;
}

void RTLSimulatorClient::compute_verilator_name_prefix(std::unordered_set<std::string> &top_names) {
    // verilator is simply TOP.[def_name], which in our cases TOP.[inst_name]
    for (auto const &def_name : top_names) {
        auto name = fmt::format("TOP.{0}.", def_name);
        hierarchy_name_prefix_map_.emplace(def_name, name);
    }
}

std::vector<std::string> RTLSimulatorClient::get_clocks_from_design() {
    if (!vpi_) return {};
    // this employ some naming heuristics to get the name
    std::vector<std::string> result;
    for (auto const &iter : hierarchy_name_prefix_map_) {
        auto const &instance_name = iter.second;
        for (auto const &clk_name : clock_names_) {
            auto const &signal_name = instance_name + clk_name;
            // test to see if there is a signal name that match
            auto *handle =
                vpi_->vpi_handle_by_name(const_cast<char *>(signal_name.c_str()), nullptr);
            if (handle) {
                // make sure it's 1 bit as well
                int width = vpi_->vpi_get(vpiSize, handle);
                if (width == 1) {
                    result.emplace_back(signal_name);
                    break;
                }
            }
        }
    }
    return result;
}

bool RTLSimulatorClient::monitor_signals(const std::vector<std::string> &signals,
                                         int (*cb_func)(p_cb_data), void *user_data) {
    std::vector<std::string> added_handles;
    added_handles.reserve(signals.size());
    for (auto const &name : signals) {
        // get full name if not yet already
        auto full_name = get_full_name(name);
        auto *handle = vpi_->vpi_handle_by_name(const_cast<char *>(full_name.c_str()), nullptr);
        if (handle) {
            // only add valid callback to avoid simulator errors
            auto callback_name = "Monitor " + full_name;
            add_call_back(callback_name, cbValueChange, cb_func, handle, user_data);
            added_handles.emplace_back(callback_name);
        } else {
            // well rollback
            for (auto const &cb_name : added_handles) {
                remove_call_back(cb_name);
            }
            return false;
        }
    }
    return true;
}

PLI_INT32 RTLSimulatorClient::get_vpi_type(vpiHandle handle) {
    std::lock_guard guard(cached_vpi_types_lock_);
    if (cached_vpi_types_.find(handle) != cached_vpi_types_.end()) [[likely]] {
        return cached_vpi_types_.at(handle);
    } else {
        auto t = vpi_->vpi_get(vpiType, handle);
        cached_vpi_types_.emplace(handle, t);
        return t;
    }
}

RTLSimulatorClient::~RTLSimulatorClient() {
    // free callback handles
    for (auto const &iter : cb_handles_) {
        vpi_->vpi_release_handle(iter.second);
    }
}

}  // namespace hgdb