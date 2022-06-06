#ifndef HGDB_MONITOR_HH
#define HGDB_MONITOR_HH

#include <functional>
#include <queue>
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
    uint64_t add_monitor_variable(const std::string& full_name, uint32_t depth,
                                  std::optional<int64_t> v);
    void remove_monitor_variable(uint64_t watch_id);
    [[nodiscard]] std::optional<uint64_t> is_monitored(const std::string& full_name,
                                                       WatchType watch_type) const;
    void set_monitor_variable_condition(uint64_t id, std::function<bool()> cond);
    [[nodiscard]] std::shared_ptr<std::optional<int64_t>> get_watched_value_ptr(
        const std::unordered_set<std::string>& var_names, WatchType type) const;
    // called every cycle
    // compute a list of signals that need to be sent
    std::vector<std::pair<uint64_t, std::optional<int64_t>>> get_watched_values(WatchType type);

    [[nodiscard]] bool empty() const { return watched_variables_.empty(); }
    [[nodiscard]] uint64_t num_watches(const std::string& name, WatchType type) const;

    // notice that each call will change the internal stored value
    std::pair<bool, std::optional<int64_t>> var_changed(uint64_t id);

private:
    // notice that monitor itself doesn't care how to get values
    // or how to resolve signal names
    // as a result, it needs to take these from the constructor
    std::function<std::optional<int64_t>(const std::string&)> get_value;

    class WatchVariable {
    public:
        WatchType type;
        std::string full_name;  // RTL name
        // enable condition associated with the watch variable. by default, it's always enabled
        std::optional<std::function<bool()>> enable_cond;

        [[nodiscard]] virtual std::optional<int64_t> get_value() const;
        virtual void set_value(std::optional<int64_t> v);
        [[nodiscard]] virtual std::shared_ptr<std::optional<int64_t>> get_value_ptr() const;

        WatchVariable(WatchType type, std::string full_name);
        WatchVariable(WatchType type, std::string full_name,
                      std::shared_ptr<std::optional<int64_t>> v);
        virtual ~WatchVariable() = default;

    private:
        std::shared_ptr<std::optional<int64_t>> value_;  // actual value
    };

    class WatchVariableBuffer : public WatchVariable {
    public:
        explicit WatchVariableBuffer(std::string full_name, uint32_t depth = 1);

        [[nodiscard]] std::optional<int64_t> get_value() const override;
        void set_value(std::optional<int64_t> v) override;
        [[nodiscard]] std::shared_ptr<std::optional<int64_t>> get_value_ptr() const override;

    private:
        uint32_t depth_;
        std::queue<std::optional<int64_t>> values_;
    };

    uint64_t watch_id_count_ = 0;
    std::unordered_map<uint64_t, std::unique_ptr<WatchVariable>> watched_variables_;

    uint32_t add_watch_var(std::unique_ptr<WatchVariable> w);
};

}  // namespace hgdb

#endif  // HGDB_MONITOR_HH
