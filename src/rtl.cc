#include "rtl.hh"

namespace hgdb {
vpiHandle RTLSimulatorClient::get_handle(const std::string &name) {
    // if we already queried this handle before
    if (handle_map_.find(name) != handle_map_.end()) {
        return handle_map_.at(name);
    } else {
        // need to query via VPI
        auto handle = const_cast<char *>(name.c_str());
        auto ptr = vpi_handle_by_name(handle, nullptr);
        if (ptr) {
            // if we actually found the handle, need to store it
            handle_map_.emplace(name, ptr);
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
    vpi_get_value(handle, &v);
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
    auto module_handle_type = vpi_get(vpiType, module_handle);
    if (module_handle_type != vpiModule) return {};

    std::unordered_map<std::string, vpiHandle> result;
    // get all net from that particular module
    auto net_iter = vpi_iterate(vpiNet, module_handle);
    if (!net_iter) return {};
    vpiHandle net_handle;
    while ((net_handle = vpi_scan(net_iter)) != nullptr) {
        char *name_raw = vpi_get_str(vpiName, net_handle);
        std::string n = name_raw;
        result.emplace(n, net_handle);
    }

    return result;
}

}  // namespace hgdb