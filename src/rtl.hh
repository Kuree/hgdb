#ifndef HGDB_RTL_HH
#define HGDB_RTL_HH

#include <optional>
#include <string>
#include <unordered_map>

#include "vpi_user.h"

namespace hgdb {
class RTLSimulatorClient {
public:
    vpiHandle get_handle(const std::string &name);
    std::optional<int64_t> get_value(const std::string &name);
    static std::optional<int64_t> get_value(vpiHandle handle);
    std::unordered_map<std::string, vpiHandle> get_module_signals(const std::string &name);

private:
    std::unordered_map<std::string, vpiHandle> handle_map_;
};
}  // namespace hgdb

#endif  // HGDB_RTL_HH
