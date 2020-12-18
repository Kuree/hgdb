#include "rtl.hh"

#include <fmt/format.h>

#include <queue>
#include <unordered_set>

namespace hgdb {

void VPIProvider::vpi_get_value(vpiHandle expr, p_vpi_value value_p) {
    return ::vpi_get_value(expr, value_p);
}

PLI_INT32 VPIProvider::vpi_get(PLI_INT32 property, vpiHandle object) {
    return ::vpi_get(property, object);
}

vpiHandle VPIProvider::vpi_iterate(PLI_INT32 type, vpiHandle refHandle) {
    return ::vpi_iterate(type, refHandle);
}

vpiHandle VPIProvider::vpi_scan(vpiHandle iterator) { return ::vpi_scan(iterator); }

char *VPIProvider::vpi_get_str(PLI_INT32 property, vpiHandle object) {
    return ::vpi_get_str(property, object);
}

vpiHandle VPIProvider::vpi_handle_by_name(char *name, vpiHandle scope) {
    return ::vpi_handle_by_name(name, scope);
}

RTLSimulatorClient::RTLSimulatorClient(const std::vector<std::string> &instance_names)
    : RTLSimulatorClient(instance_names, nullptr) {}

RTLSimulatorClient::RTLSimulatorClient(const std::vector<std::string> &instance_names,
                                       std::unique_ptr<AVPIProvider> vpi) {
    // if vpi provider is null, we use the system default one
    if (!vpi) {
        vpi_ = std::make_unique<VPIProvider>();
    } else {
        // we take the ownership
        vpi_ = std::move(vpi);
    }
    std::unordered_set<std::string> top_names;
    for (auto const &name : instance_names) {
        auto top = get_path(name).first;
        top_names.emplace(top);
    }
}

vpiHandle RTLSimulatorClient::get_handle(const std::string &name) {
    auto full_name = get_full_name(name);
    // if we already queried this handle before
    if (handle_map_.find(full_name) != handle_map_.end()) {
        return handle_map_.at(full_name);
    } else {
        // need to query via VPI
        auto handle = const_cast<char *>(full_name.c_str());
        auto ptr = vpi_->vpi_handle_by_name(handle, nullptr);
        if (ptr) {
            // if we actually found the handle, need to store it
            handle_map_.emplace(full_name, ptr);
        }
        return ptr;
    }
}

std::optional<int64_t> RTLSimulatorClient::get_value(vpiHandle handle) {
    if (!handle) {
        return std::nullopt;
    }
    s_vpi_value v;
    v.format = vpiIntVal;
    vpi_->vpi_get_value(handle, &v);
    int64_t result = v.value.integer;
    return result;
}

std::optional<int64_t> RTLSimulatorClient::get_value(const std::string &name) {
    auto handle = get_handle(name);
    return get_value(handle);
}

std::unordered_map<std::string, vpiHandle> RTLSimulatorClient::get_module_signals(
    const std::string &name) {
    auto module_handle = get_handle(name);
    if (!module_handle) return {};
    // need to make sure it is module type
    auto module_handle_type = vpi_->vpi_get(vpiType, module_handle);
    if (module_handle_type != vpiModule) return {};

    std::unordered_map<std::string, vpiHandle> result;
    // get all net from that particular module
    auto net_iter = vpi_->vpi_iterate(vpiNet, module_handle);
    if (!net_iter) return {};
    vpiHandle net_handle;
    while ((net_handle = vpi_->vpi_scan(net_iter)) != nullptr) {
        char *name_raw = vpi_->vpi_get_str(vpiName, net_handle);
        std::string n = name_raw;
        result.emplace(n, net_handle);
    }

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
        return prefix + name;
    }
}

std::pair<std::string, std::string> RTLSimulatorClient::get_path(const std::string &name) {
    auto pos = name.find_first_of('.');
    auto top = name.substr(0, pos - 1);
    auto n = name.substr(pos + 1);
    return {top, n};
}

}  // namespace hgdb