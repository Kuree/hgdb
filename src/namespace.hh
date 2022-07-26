#ifndef HGDB_NAMESPACE_HH
#define HGDB_NAMESPACE_HH
#include "monitor.hh"
#include "rtl.hh"

namespace hgdb {
struct DebuggerNamespace {
    uint32_t id = 0;
    std::string def_name;
    std::unique_ptr<RTLSimulatorClient> rtl;
    std::unique_ptr<Monitor> monitor;

    explicit DebuggerNamespace(uint32_t id, std::unique_ptr<RTLSimulatorClient> rtl);
};

class SymbolTableProvider;

class DebuggerNamespaceManager {
public:
    DebuggerNamespaceManager() = default;
    DebuggerNamespace *add_namespace(std::shared_ptr<AVPIProvider> vpi);

    DebuggerNamespace *operator[](uint64_t index) { return namespaces_[index].get(); }
    RTLSimulatorClient *default_rtl() const;
    DebuggerNamespace *default_namespace() const;
    uint64_t default_id() const { return 0; }
    void compute_instance_mapping(SymbolTableProvider *db);
    [[nodiscard]] auto empty() const { return namespaces_.empty(); }
    [[nodiscard]] auto size() const { return namespaces_.size(); }
    auto begin() const { return namespaces_.begin(); }
    auto end() const { return namespaces_.end(); }

    const std::vector<DebuggerNamespace *> &get_namespaces(
        const std::optional<std::string> &def_name);

private:
    std::vector<std::unique_ptr<DebuggerNamespace>> namespaces_;
    std::unordered_map<std::string, std::vector<DebuggerNamespace *>> mapped_namespaces_;
    std::unordered_map<uint32_t, std::string> bp_name_name_;

    void compute_mapping();
};
};  // namespace hgdb

#endif  // HGDB_NAMESPACE_HH
