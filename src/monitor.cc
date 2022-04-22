#include "monitor.hh"

#include "debug.hh"

namespace hgdb {
Monitor::Monitor() {
    // everything is 0 if not set up
    get_value = [](const std::string&) { return 0; };
}

Monitor::Monitor(std::function<std::optional<int64_t>(const std::string&)> get_value)
    : get_value(std::move(get_value)) {}

uint64_t Monitor::add_monitor_variable(const std::string& full_name, WatchType watch_type) {
    // we assume full name is checked already
    // need to search if we have the same name already
    auto watched = is_monitored(full_name, watch_type);
    if (watched) {
        return *watched;
    }
    // old clang-tidy reports memory leak for .value = make_shared
    auto value_ptr = std::make_shared<std::optional<int64_t>>();
    auto w = WatchVariable{.type = watch_type, .full_name = full_name, .value = value_ptr};
    watched_variables_.emplace(watch_id_count_, w);
    return watch_id_count_++;
}

uint64_t Monitor::add_monitor_variable(const std::string& full_name, WatchType watch_type,
                                       std::shared_ptr<std::optional<int64_t>> value) {
    auto v = add_monitor_variable(full_name, watch_type);
    watched_variables_.at(v).value = std::move(value);
    return v;
}

void Monitor::remove_monitor_variable(uint64_t watch_id) {
    if (watched_variables_.find(watch_id) != watched_variables_.end()) {
        watched_variables_.erase(watch_id);
    }
}

std::optional<uint64_t> Monitor::is_monitored(const std::string& full_name,
                                              WatchType watch_type) const {
    for (auto const& [id, var] : watched_variables_) {
        if (var.full_name == full_name && var.type == watch_type) [[unlikely]] {
            // reuse the existing ID
            return id;
        }
    }
    return std::nullopt;
}

std::shared_ptr<std::optional<int64_t>> Monitor::get_watched_value_ptr(
    const std::unordered_set<std::string>& var_names, WatchType type) const {
    for (auto const& [id, var] : watched_variables_) {
        if (var_names.find(var.full_name) != var_names.end() && var.type == type) {
            // reuse the existing ID
            return var.value;
        }
    }
    return nullptr;
}

std::vector<std::pair<uint64_t, std::string>> Monitor::get_watched_values(WatchType type) {
    std::vector<std::pair<uint64_t, std::string>> result;
    // this is the maximum size
    result.reserve(watched_variables_.size());

    for (auto& [watch_id, watch_var] : watched_variables_) {
        if (watch_var.type != type) continue;
        switch (watch_var.type) {
            case WatchType::breakpoint:
            case WatchType::clock_edge: {
                auto value = get_value(watch_var.full_name);
                std::string str_value;
                if (value) {
                    str_value = std::to_string(*value);
                } else {
                    str_value = Debugger::error_value_str;
                }
                result.emplace_back(std::make_pair(watch_id, str_value));
                break;
            }
            case WatchType::data:
            case WatchType::changed: {
                // only if values are changed
                auto [changed, value] = var_changed(watch_id);
                if (changed) {
                    auto str_value = std::to_string(*value);
                    result.emplace_back(std::make_pair(watch_id, str_value));
                }
                break;
            }
            case WatchType::delay_clock_edge: {
                // TODO: add logic to store previous cycle value
            }
        }
    }

    return result;
}

uint64_t Monitor::num_watches(const std::string& name, WatchType type) const {
    uint64_t result = 0;
    for (auto const& iter : watched_variables_) {
        if (iter.second.full_name == name && iter.second.type == type) {
            result++;
        }
    }

    return result;
}

std::pair<bool, std::optional<int64_t>> Monitor::var_changed(uint64_t id) {
    if (watched_variables_.find(id) == watched_variables_.end()) [[unlikely]] {
        return {false, {}};
    }
    auto& watch_var = watched_variables_.at(id);
    auto value = get_value(watch_var.full_name);
    if (value) {
        bool changed = false;
        if (!watch_var.value->has_value() || watch_var.value->value() != *value) changed = true;
        if (changed) {
            *watch_var.value = value;
        }
        return {changed, value};
    }
    return {false, {}};
}

}  // namespace hgdb