#ifndef HGDB_RTL_HH
#define HGDB_RTL_HH

#include <memory>
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
    virtual ~AVPIProvider() = default;
};

class VPIProvider : public AVPIProvider {
    void vpi_get_value(vpiHandle expr, p_vpi_value value_p) override;
    PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) override;
    vpiHandle vpi_scan(vpiHandle iterator) override;
    char *vpi_get_str(PLI_INT32 property, vpiHandle object) override;
    vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) override;
};

class RTLSimulatorClient {
public:
    explicit RTLSimulatorClient(const std::vector<std::string> &instance_names);
    RTLSimulatorClient(const std::vector<std::string> &instance_names,
                       std::unique_ptr<AVPIProvider> vpi);
    vpiHandle get_handle(const std::string &name);
    std::optional<int64_t> get_value(const std::string &name);
    std::optional<int64_t> get_value(vpiHandle handle);
    std::unordered_map<std::string, vpiHandle> get_module_signals(const std::string &name);

private:
    std::unordered_map<std::string, vpiHandle> handle_map_;
    // it is a map just in case there are separated tops being generated
    // in this case, each top needs to get mapped to a different hierarchy
    std::unordered_map<std::string, std::string> hierarchy_name_prefix_map_;
    // VPI provider
    std::unique_ptr<AVPIProvider> vpi_;

    std::string get_full_name(const std::string &name) const;
    static std::pair<std::string, std::string> get_path(const std::string &name);
    void compute_hierarchy_name_prefix(std::unordered_set<std::string>& top_names);
};
}  // namespace hgdb

#endif  // HGDB_RTL_HH