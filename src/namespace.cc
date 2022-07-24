#include "namespace.hh"

#include "symbol.hh"
#include "util.hh"

namespace hgdb {

DebuggerNamespace::DebuggerNamespace(uint32_t id, std::unique_ptr<RTLSimulatorClient> rtl)
    : id(id), rtl(std::move(rtl)) {
    monitor = std::make_unique<Monitor>(this->rtl.get());
}

DebuggerNamespace *DebuggerNamespaceManager::add_namespace(std::shared_ptr<AVPIProvider> vpi) {
    auto rtl = std::make_unique<RTLSimulatorClient>(std::move(vpi));
    auto &ns = namespaces_.emplace_back(
        std::make_unique<DebuggerNamespace>(namespaces_.size(), std::move(rtl)));
    return ns.get();
}

RTLSimulatorClient *DebuggerNamespaceManager::default_rtl() {
    return namespaces_.empty() ? nullptr : namespaces_[0]->rtl.get();
}

void DebuggerNamespaceManager::compute_instance_mapping(SymbolTableProvider *db) {
    // get all the instance names
    auto instances = db->get_instance_names();
    if (!default_rtl()) return;

    auto mapping =
        default_rtl()->compute_instance_mapping(instances, default_rtl()->vpi()->has_defname());

    for (auto i = 1u; i < mapping.size(); i++) {
        add_namespace(default_rtl()->vpi());
    }

    for (auto i = 0u; i < mapping.size(); i++) {
        namespaces_[i]->rtl->set_mapping(mapping[i].first, mapping[i].second);
        namespaces_[i]->def_name = mapping[i].first;
    }
    compute_mapping();
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

}  // namespace hgdb