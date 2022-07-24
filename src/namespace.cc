#include "namespace.hh"

#include "util.hh"

namespace hgdb {

DebuggerNamespace::DebuggerNamespace(uint32_t id, std::unique_ptr<RTLSimulatorClient> rtl)
    : id(id), rtl(std::move(rtl)) {
    monitor = std::make_unique<Monitor>(this->rtl.get());
}

DebuggerNamespaceManager::DebuggerNamespaceManager() {
    get_instance_name_ = [](uint32_t) { return std::nullopt; };
}

void DebuggerNamespaceManager::add_namespace(std::shared_ptr<AVPIProvider> vpi) {
    auto rtl = std::make_unique<RTLSimulatorClient>(std::move(vpi));
    namespaces_.emplace_back(
        std::make_unique<DebuggerNamespace>(namespaces_.size(), std::move(rtl)));
}

RTLSimulatorClient *DebuggerNamespaceManager::default_rtl() {
    return namespaces_.empty() ? nullptr : namespaces_[0]->rtl.get();
}

void DebuggerNamespaceManager::compute_mapping() {
    for (auto &ns : namespaces_) {
        mapped_namespaces_[ns->def_name].emplace_back(ns.get());
    }
}

const std::vector<DebuggerNamespace *> &DebuggerNamespaceManager::get_namespaces(
    const std::optional<std::string> &instance_name) {
    const static std::vector<DebuggerNamespace *> empty = {};
    if (!instance_name) return empty;
    auto def_name = util::get_tokens(*instance_name, ".")[0];
    if (mapped_namespaces_.find(def_name) != mapped_namespaces_.end()) [[likely]] {
        return mapped_namespaces_.at(def_name);
    } else {
        return empty;
    }
}

void DebuggerNamespaceManager::set_get_instance_name(
    std::function<std::optional<std::string>(uint32_t)> func) {
    get_instance_name_ = std::move(func);
}

}  // namespace hgdb