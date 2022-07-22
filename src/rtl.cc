#include "rtl.hh"

#include <fmt/format.h>

#include <cstdarg>
#include <queue>
#include <unordered_set>

#include "log.hh"
#include "sv_vpi_user.h"
#include "util.hh"

namespace hgdb {

void VPIProvider::vpi_get_value(vpiHandle expr, p_vpi_value value_p) {
    if (use_lock_getting_value_) {
        std::lock_guard guard(vpi_lock_);
        return ::vpi_get_value(expr, value_p);
    } else {
        // if we know for certain this is no contention when getting values
        // we can disable this lock
        return ::vpi_get_value(expr, value_p);
    }
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

vpiHandle VPIProvider::vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p,
                                     PLI_INT32 flags) {
    std::lock_guard guard(vpi_lock_);
    return ::vpi_put_value(object, value_p, time_p, flags);
}

RTLSimulatorClient::RTLSimulatorClient(std::shared_ptr<AVPIProvider> vpi) {
    initialize_vpi(std::move(vpi));
}

RTLSimulatorClient::RTLSimulatorClient(const std::vector<std::string> &instance_names)
    : RTLSimulatorClient(instance_names, nullptr) {}

RTLSimulatorClient::RTLSimulatorClient(const std::vector<std::string> &instance_names,
                                       std::shared_ptr<AVPIProvider> vpi) {
    initialize(instance_names, std::move(vpi));
}

void RTLSimulatorClient::initialize(const std::vector<std::string> &instance_names,
                                    std::shared_ptr<AVPIProvider> vpi) {
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
    if (custom_hierarchy_func_) {
        hierarchy_name_prefix_map_ = (*custom_hierarchy_func_)(top_names);
    } else {
        compute_hierarchy_name_prefix(top_names);
    }
}

void RTLSimulatorClient::set_custom_hierarchy_func(
    const std::function<std::unordered_map<std::string, std::string>(
        const std::unordered_set<std::string> &)> &func) {
    custom_hierarchy_func_ = func;
}

void RTLSimulatorClient::initialize_vpi(std::shared_ptr<AVPIProvider> vpi) {
    // if vpi provider is null, we use the system default one
    if (!vpi) {
        vpi_ = std::make_shared<VPIProvider>();
    } else {
        // we take the ownership
        vpi_ = std::move(vpi);
    }
    // set simulator information
    set_simulator_info();

    // compute the vpiNet target. this is a special case for Verilator
    vpi_net_target_ = is_verilator() ? vpiReg : vpiNet;
    const static bool is_commercial = is_vcs() || is_xcelium();
    vpi_->set_use_lock_getting_value(is_commercial);
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
        // we can assume that handle_map_ is well protected
        // strip off the trailing tokens until it becomes a variable

        // also, if the last token contains ':', we need to handle slice
        // properly using the mock slice handle
        bool has_slice = tokens.back().find_first_of(':') != std::string::npos;
        vpiHandle ptr = nullptr;
        if (has_slice) [[unlikely]] {
            // if it's a slice, last chance to get it right
            auto handle_name = util::join(
                tokens.begin(), tokens.begin() + static_cast<uint32_t>(tokens.size()) - 1, ".");
            ptr = get_handle_raw(handle_name);
        }

        if (!ptr) [[likely]] {
            auto array_size_end =
                static_cast<long>(has_slice ? tokens.size() - 2 : tokens.size() - 1);

            for (auto i = array_size_end; i > 0; i--) {
                auto pos = tokens.begin() + i;
                auto handle_name = util::join(tokens.begin(), pos, ".");
                ptr = get_handle_raw(handle_name);

                if (ptr) {
                    auto type = get_vpi_type(ptr);
                    if (type != vpiModule) {
                        // best effort
                        // notice that we only support array indexing, since struct
                        // access should be handled by simulator properly
                        ptr = access_arrays(pos, tokens.begin() + array_size_end + 1, ptr);
                        break;
                    }
                }
            }
        }

        if (has_slice && ptr) [[unlikely]] {
            // need to create fake slices
            // we optimize for cases where there is no slices
            ptr = add_mock_slice_vpi(ptr, tokens.back());
        }

        return ptr;
    }
}

bool RTLSimulatorClient::is_valid_signal(const std::string &name) {
    auto *handle = get_handle(name);
    if (!handle) return false;
    auto type = get_vpi_type(handle);
    return type == vpiReg || type == vpiNet || type == vpiRegArray || type == vpiRegBit ||
           type == vpiNetArray || type == vpiNetBit || type == vpiPartSelect ||
           type == vpiMemoryWord;
}

vpiHandle RTLSimulatorClient::access_arrays(StringIterator begin, StringIterator end,
                                            vpiHandle var_handle) {
    auto it = begin;
    while (it != end) {
        auto idx = *it;
        if (!std::all_of(idx.begin(), idx.end(), ::isdigit)) {
            return nullptr;
        }
        auto index_value = std::stoi(idx);
        var_handle = vpi_->vpi_handle_by_index(var_handle, index_value);
        if (!var_handle) return nullptr;
        it++;
    }
    return var_handle;
}

int64_t get_slice(int64_t value, const std::tuple<vpiHandle, uint32_t, uint32_t> &info) {
    // notice that hi and lo are inclusive
    auto [parent, hi, lo] = info;
    auto v = static_cast<uint64_t>(value);
    auto hi_mask = ~(std::numeric_limits<uint64_t>::max() << (hi + 1));
    v = v & hi_mask;
    v = v >> lo;
    return static_cast<int64_t>(v);
}

std::optional<int64_t> RTLSimulatorClient::get_value(vpiHandle handle) {
    if (!handle) [[unlikely]] {
        return std::nullopt;
    }
    // get value size. Verilator will freak out if the width is larger than 64
    // notice this is mostly cached result
    if (is_verilator()) {
        auto width = get_vpi_size(handle);
        if (width > 64) [[unlikely]] {
            auto *name = vpi_->vpi_get_str(vpiName, handle);
            log::log(log::log_level::info,
                     fmt::format("{0} is too large to display as an integer", name));
            return {};
        }
    }

    vpiHandle request_handle = handle;

    // if we have mock vpi handle, use it
    // optimize for unlikely
    bool is_slice_handle = mock_slice_handles_.find(handle) != mock_slice_handles_.end();
    if (is_slice_handle) [[unlikely]]
        handle = std::get<0>(mock_slice_handles_.at(handle));

    s_vpi_value v;
    v.format = vpiIntVal;
    vpi_->vpi_get_value(handle, &v);
    int64_t result = v.value.integer;

    if (is_slice_handle) [[unlikely]] {
        result = get_slice(result, mock_slice_handles_.at(request_handle));
    }

    return result;
}

std::optional<uint32_t> RTLSimulatorClient::get_signal_width(vpiHandle handle) {
    auto w = get_vpi_size(handle);
    if (w == 0) [[unlikely]] {
        return std::nullopt;
    } else {
        return w;
    }
}

std::optional<int64_t> RTLSimulatorClient::get_value(const std::string &name) {
    auto *handle = get_handle(name);
    return get_value(handle);
}

std::optional<std::string> RTLSimulatorClient::get_str_value(const std::string &name) {
    auto *handle = get_handle(name);
    return get_str_value(handle);
}

std::string get_slice(const std::string &value,
                      const std::tuple<vpiHandle, uint32_t, uint32_t> &info) {
    auto [parent, hi, lo] = info;
    // notice that it's in reverse order!
    // hi and lo are inclusive as in RTL
    if (lo > value.size() - 1) {
        return "0";
    }

    auto pos = std::min<uint32_t>(value.size() - hi - 1, 0);
    auto lo_pos = std::max<uint32_t>(value.size() - lo - 1, 0);
    auto result = value.substr(pos, lo_pos - pos + 1);
    // now we have binary result, need to convert it into hex
    // pad string to multiple of 4
    while (result.size() % 4) {
        result = fmt::format("0{0}", result);
    }
    std::string s;
    for (auto i = 0u; i < result.size(); i += 4) {
        auto str = result.substr(i, 4);
        auto v = std::stoul(str, nullptr, 2);
        s.append(fmt::format("{0:X}", v));
    }

    return s;
}

std::optional<std::string> RTLSimulatorClient::get_str_value(vpiHandle handle) {
    if (!handle) [[unlikely]] {
        return std::nullopt;
    }
    auto type = get_vpi_type(handle);
    if (type == vpiModule) [[unlikely]] {
        return std::nullopt;
    }

    vpiHandle request_handle = handle;

    bool is_slice = mock_slice_handles_.find(handle) != mock_slice_handles_.end();
    handle = is_slice ? std::get<0>(mock_slice_handles_.at(handle)) : handle;

    s_vpi_value v;
    v.format = is_slice ? vpiBinStrVal : vpiHexStrVal;
    vpi_->vpi_get_value(handle, &v);
    std::string result = v.value.str;
    if (is_slice) [[unlikely]] {
        result = get_slice(result, mock_slice_handles_.at(request_handle));
    }
    // we only add 0x to any signal that has more than 1bit
    auto width = get_vpi_size(request_handle);
    if (width > 1) result = fmt::format("0x{0}", result);
    return result;
}

bool RTLSimulatorClient::set_value(vpiHandle handle, int64_t value) {
    if (!handle) return false;
    s_vpi_value vpi_value;
    vpi_value.value.integer = static_cast<int>(value);
    vpi_value.format = vpiIntVal;

    // If the flag argument also has the bit mask vpiReturnEvent,
    // vpi_put_value() shall return a handle of type vpiSchedEvent to the newly scheduled event,
    // provided there is some form of a delay and an event is scheduled.
    // If the bit mask is not used, or if no delay is used, or if an event is not scheduled,
    // the return value shall be NULL.
    // based on the spec, there is no way to tell whether it is successful or not
    // as a result, we use a magic number to indicate if it fails for emulator
    auto *res = vpi_->vpi_put_value(handle, &vpi_value, nullptr, vpiNoDelay);
    auto *invalid_value = (vpiHandle)std::numeric_limits<uint64_t>::max();
    return invalid_value != res;
}

bool RTLSimulatorClient::set_value(const std::string &name, int64_t value) {
    auto *handle = get_handle(name);
    return set_value(handle, value);
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
    auto *net_iter = vpi_->vpi_iterate(static_cast<int>(vpi_net_target_), module_handle);
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
        // we haven't seen this top. it has to be an error since we require top name
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

std::string RTLSimulatorClient::get_full_name(vpiHandle handle) {
    auto const *res = vpi_->vpi_get_str(vpiFullName, handle);
    if (res) {
        return {res};
    } else {
        return {};
    }
}

bool RTLSimulatorClient::is_absolute_path(const std::string &name) const {
    auto const [top, path] = get_path(name);
    return hierarchy_name_prefix_map_.find(top) != hierarchy_name_prefix_map_.end();
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
    if (handle) {
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
}

void RTLSimulatorClient::stop_sim(finish_value value) {
    vpi_->vpi_control(vpiStop, static_cast<int>(value));
}

void RTLSimulatorClient::finish_sim(finish_value value) {
    vpi_->vpi_control(vpiFinish, static_cast<int>(value));
}

// need to resolve special patterns
std::string resolve_rtl_path(const std::string &path) {
    // optimizing for most cases
    if (path.find_first_of('$') == std::string::npos) [[likely]] {
        return path;
    }

    auto tokens = util::get_tokens(path, ".");
    for (auto i = 1u; i < tokens.size(); i++) {
        if (tokens[i] == "$parent") {
            tokens[i] = "";
            tokens[i - 1] = "";
        }
    }
    // filter empty ones
    tokens.erase(
        std::remove_if(tokens.begin(), tokens.end(), [](auto const &s) { return s.empty(); }),
        tokens.end());
    auto res = fmt::format("{0}", fmt::join(tokens.begin(), tokens.end(), "."));
    return res;
}

// NOLINTNEXTLINE
std::vector<std::pair<std::string, std::string>> RTLSimulatorClient::resolve_rtl_variable(
    const std::string &front_name, std::string rtl_name) {
    rtl_name = resolve_rtl_path(rtl_name);
    auto *handle = get_handle(rtl_name);
    // if null handle, we will report it as ERROR, which will be handled elsewhere
    if (!handle) return {{front_name, rtl_name}};
    auto type = get_vpi_type(handle);

    auto iterate_type = [handle, this, &front_name](
                            int property, std::vector<std::pair<std::string, std::string>> &res) {
        if (auto *it = vpi_->vpi_iterate(property, handle)) {
            while (auto *h = vpi_->vpi_scan(it)) {
                auto *rtl = vpi_->vpi_get_str(vpiFullName, h);
                auto *n = vpi_->vpi_get_str(vpiName, h);
                // make it recursive to account for
                auto new_res = resolve_rtl_variable(front_name + "." + n, rtl);
                res.insert(res.end(), new_res.begin(), new_res.end());
            }
        }
    };

    auto array_iteration = [handle, this, &front_name, &rtl_name](
                               int property,
                               std::vector<std::pair<std::string, std::string>> &res) {
        if (auto *it = vpi_->vpi_iterate(property, handle)) {
            uint64_t idx = 0;
            // notice this will only work with indices that start from 0
            // Xcelium scanning does not work!
            while (vpi_->vpi_scan(it)) {
                auto sub_rtl_name = fmt::format("{0}[{1}]", rtl_name, idx);
                auto sub_var_name = fmt::format("{0}.{1}", front_name, idx);
                // make sure it's a valid rtl
                if (!is_valid_signal(sub_rtl_name)) break;
                auto new_res = resolve_rtl_variable(sub_var_name, sub_rtl_name);
                res.insert(res.end(), new_res.begin(), new_res.end());
                idx++;
            }
        }
    };

    auto brute_force_array = [this, handle, &front_name, &rtl_name,
                              type](std::vector<std::pair<std::string, std::string>> &res) {
        // Xcelium use vpiReg for packed array, and you can't use vpiElement iteration for
        // some eason
        // per LRM 37.16 29), a spec-compliant simulator should allow this
        // we have to use brute force
        auto first_element = fmt::format("{0}[0]", rtl_name);
        if (auto *first_handle = get_handle(first_element)) {
            auto is_array = vpi_->vpi_get(vpiVector, first_handle);
            if (is_array != vpiError && is_array != vpiUndefined && is_array != 0) {
                auto size = vpi_->vpi_get(vpiSize, handle);
                if (type != vpiRegArray && type != vpiNetArray) {
                    auto element_size = vpi_->vpi_get(vpiSize, first_handle);
                    size = size / element_size;
                }
                for (auto idx = 0; idx < size; idx++) {
                    auto sub_rtl_name = fmt::format("{0}[{1}]", rtl_name, idx);
                    auto sub_var_name = fmt::format("{0}.{1}", front_name, idx);
                    auto new_res = resolve_rtl_variable(sub_var_name, sub_rtl_name);
                    res.insert(res.end(), new_res.begin(), new_res.end());
                }
            }
        }
    };
    std::vector<std::pair<std::string, std::string>> res;
    switch (type) {
            // verilator treat interface as a module
        case vpiModule:
        case vpiInterface: {
            if (is_vcs()) {
                log::log(log::log_level::info, "VCS interface not supported");
                return {};
            }
            auto vpi_types = {vpiNet,      vpiReg,      vpiMemory,
                              vpiNetArray, vpiRegArray, vpiInterfacePort};
            for (auto vpi_type : vpi_types) {
                iterate_type(vpi_type, res);
            }
            break;
        }
        case vpiStructVar:
        case vpiStructNet: {
            iterate_type(vpiMember, res);
            break;
        }
        // verilator uses these values even for packed array
        case vpiMemory:
        case vpiNetArray:
        case vpiRegArray: {
            if (!is_verilator() && !is_mock()) {
                brute_force_array(res);
            } else {
                array_iteration(vpiRange, res);
            }
            break;
        }
        default: {
            if (!is_verilator()) {
                brute_force_array(res);
            }
        }
    }
    if (res.empty()) [[likely]] {
        // most cases it's a wire
        return {{front_name, rtl_name}};
    } else {
        return res;
    }
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
    is_mock_ = sim_info_.name == "RTLMock";
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
        bool error = true;
        if (handle) {
            // only add valid callback to avoid simulator errors
            auto callback_name = "Monitor " + full_name;
            auto const *r = add_call_back(callback_name, cbValueChange, cb_func, handle, user_data);
            if (r) {
                added_handles.emplace_back(callback_name);
                error = false;
            }
        }

        if (error) {
            log::log(log::log_level::error,
                     fmt::format("Unable to register callback to monitor signal {0}", full_name));
            // well rollback
            for (auto const &cb_name : added_handles) {
                remove_call_back(cb_name);
            }
            return false;
        }
    }
    return true;
}

std::unordered_set<std::string> RTLSimulatorClient::callback_names() {
    std::lock_guard guard(cb_handles_lock_);
    std::unordered_set<std::string> result;
    for (auto const &iter : cb_handles_) {
        result.emplace(iter.first);
    }

    return result;
}

bool RTLSimulatorClient::reverse_last_posedge(const std::vector<vpiHandle> &clk_handles) {
    return rewind(get_simulation_time(), clk_handles);
}

bool RTLSimulatorClient::rewind(uint64_t time, const std::vector<vpiHandle> &clk_handles) {
    AVPIProvider::rewind_data data{.time = time, .clock_signals = clk_handles};

    return vpi_->vpi_rewind(&data);
}

void RTLSimulatorClient::set_vpi_allocator(const std::function<vpiHandle()> &func) {
    vpi_allocator_ = func;
}

PLI_INT32 RTLSimulatorClient::get_vpi_type(vpiHandle handle) {
    if (!handle) return vpiError;
    std::lock_guard guard(cached_vpi_types_lock_);
    if (cached_vpi_types_.find(handle) != cached_vpi_types_.end()) [[likely]] {
        return cached_vpi_types_.at(handle);
    } else {
        auto t = vpi_->vpi_get(vpiType, handle);
        cached_vpi_types_.emplace(handle, t);
        return t;
    }
}

uint32_t RTLSimulatorClient::get_vpi_size(vpiHandle handle) {
    if (!handle) return 0;
    std::lock_guard guard(cached_vpi_size_lock_);
    if (cached_vpi_size_.find(handle) != cached_vpi_size_.end()) [[likely]] {
        return cached_vpi_size_.at(handle);
    } else {
        auto t = vpi_->vpi_get(vpiSize, handle);
        if (t != vpiUndefined) [[likely]] {
            cached_vpi_size_.emplace(handle, t);
            return t;
        }
        return 0;
    }
}

RTLSimulatorClient::~RTLSimulatorClient() {
    // free callback handles
    for (auto const &iter : cb_handles_) {
        vpi_->vpi_release_handle(iter.second);
    }
}

std::unordered_map<std::string, std::string> RTLSimulatorClient::get_top_mapping() const {
    std::unordered_map<std::string, std::string> result;
    for (auto const &[src_name, tb_name] : hierarchy_name_prefix_map_) {
        auto name_from =
            src_name.ends_with('.') ? src_name.substr(0, src_name.size() - 1) : src_name;
        auto name_to = tb_name.ends_with('.') ? tb_name.substr(0, tb_name.size() - 1) : tb_name;
        result.emplace(name_from, name_to);
    }
    return result;
}

std::optional<std::pair<uint32_t, uint32_t>> extract_slice(const std::string &token) {
    auto nums = util::get_tokens(token, ":");
    if (nums.size() != 2) return {};

    uint32_t hi = 0, lo = std::numeric_limits<uint32_t>::max();
    for (auto const &num : nums) {
        auto n = util::stoul(num);
        if (!n) return {};
        if (*n < lo) lo = *n;
        if (*n > hi) hi = *n;
    }
    return std::make_pair(hi, lo);
}

vpiHandle RTLSimulatorClient::add_mock_slice_vpi(vpiHandle parent, const std::string &slice) {
    auto slice_num = extract_slice(slice);
    if (!slice_num) return nullptr;
    auto [hi, lo] = *slice_num;
    auto *new_handle = vpi_allocator_ ? (*vpi_allocator_)() : ++mock_slice_handle_counter_;
    mock_slice_handles_.emplace(new_handle, std::make_tuple(parent, hi, lo));
    return new_handle;
}

vpiHandle RTLSimulatorClient::get_handle_raw(const std::string &handle_name) {
    vpiHandle ptr = nullptr;
    if (handle_map_.find(handle_name) != handle_map_.end()) {
        ptr = handle_map_.at(handle_name);
    } else {
        ptr = vpi_->vpi_handle_by_name(const_cast<char *>(handle_name.c_str()), nullptr);
        if (ptr) {
            handle_map_.emplace(handle_name, ptr);
        }
    }
    return ptr;
}

}  // namespace hgdb