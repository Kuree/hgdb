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
    for (auto const& [id, var] : watched_variables_) {
        if (var.full_name == full_name && var.type == watch_type) [[unlikely]] {
            // reuse the existing ID
            return id;
        }
    }
    watched_variables_.emplace(
        watch_id_count_, WatchVariable{.type = watch_type, .full_name = full_name, .value = 0});
    return watch_id_count_++;
}

void Monitor::remove_monitor_variable(uint64_t watch_id) {
    if (watched_variables_.find(watch_id) != watched_variables_.end()) {
        watched_variables_.erase(watch_id);
    }
}

std::vector<std::pair<uint64_t, std::string>> Monitor::get_watched_values(bool has_breakpoint) {
    std::vector<std::pair<uint64_t, std::string>> result;
    // this is the maximum size
    result.reserve(watched_variables_.size());

    for (auto& [watch_id, watch_var] : watched_variables_) {
        switch (watch_var.type) {
            case WatchType::breakpoint: {
                // only if we hit a breakpoint
                if (has_breakpoint) {
                    auto value = get_value(watch_var.full_name);
                    std::string str_value;
                    if (value) {
                        str_value = std::to_string(*value);
                    } else {
                        str_value = Debugger::error_value_str;
                    }
                    result.emplace_back(std::make_pair(watch_id, str_value));
                }
                break;
            }
            case WatchType::clock_edge: {
                // only if we are not in a breakpoint
                if (!has_breakpoint) {
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

}  // namespace hgdb