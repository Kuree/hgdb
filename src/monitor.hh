#ifndef HGDB_MONITOR_HH
#define HGDB_MONITOR_HH

#include <functional>
#include <unordered_set>

#include "proto.hh"

namespace hgdb {

class Monitor {
public:
    using WatchType = MonitorRequest::MonitorType;

    Monitor();
    explicit Monitor(std::function<std::optional<int64_t>(const std::string&)> get_value);
    uint64_t add_monitor_variable(const std::string& full_name, WatchType watch_type);
    uint64_t add_monitor_variable(const std::string& full_name, WatchType watch_type,
                                  std::shared_ptr<std::optional<int64_t>> value);
    void remove_monitor_variable(uint64_t watch_id);
    // called every cycle
    // compute a list of signals that need to be sent
    std::vector<std::pair<uint64_t, std::string>> get_watched_values(WatchType type);

    [[nodiscard]] bool empty() const { return watched_variables_.empty(); }
    [[nodiscard]] uint64_t num_watches(const std::string& name, WatchType type) const;

    // notice that each call will change the internal stored value
    std::pair<bool, std::optional<int64_t>> var_changed(uint64_t id);

private:
    // notice that monitor itself doesn't care how to get values
    // or how to resolve signal names
    // as a result, it needs to take these from the constructor
    std::function<std::optional<int64_t>(const std::string&)> get_value;

    struct WatchVariable {
        WatchType type;
        std::string full_name;                          // RTL name
        std::shared_ptr<std::optional<int64_t>> value;  // actual value
    };

    uint64_t watch_id_count_ = 0;
    std::unordered_map<uint64_t, WatchVariable> watched_variables_;
};

}  // namespace hgdb

#endif  // HGDB_MONITOR_HH
