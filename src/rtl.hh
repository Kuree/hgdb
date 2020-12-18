#ifndef HGDB_RTL_HH
#define HGDB_RTL_HH

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "vpi_user.h"

namespace hgdb {
class RTLSimulatorClient {
public:
    explicit RTLSimulatorClient(const std::vector<std::string> &instance_names);
    vpiHandle get_handle(const std::string &name);
    std::optional<int64_t> get_value(const std::string &name);
    static std::optional<int64_t> get_value(vpiHandle handle);
    std::unordered_map<std::string, vpiHandle> get_module_signals(const std::string &name);

private:
    std::unordered_map<std::string, vpiHandle> handle_map_;
    // it is a map just in case there are separated tops being generated
    // in this case, each top needs to get mapped to a different hierarchy
    std::unordered_map<std::string, std::string> hierarchy_name_prefix_map_;

    std::string get_full_name(const std::string &name) const;
    static std::pair<std::string, std::string> get_path(const std::string &name);
};
}  // namespace hgdb

#endif  // HGDB_RTL_HH
