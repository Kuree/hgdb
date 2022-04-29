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
    auto w = WatchVariable(watch_type, full_name);
    watched_variables_.emplace(watch_id_count_, w);
    return watch_id_count_++;
}

uint64_t Monitor::add_monitor_variable(const std::string& full_name, WatchType watch_type,
                                       std::shared_ptr<std::optional<int64_t>> value) {
    auto watched = is_monitored(full_name, watch_type);
    if (watched) {
        return *watched;
    }
    auto w = WatchVariable(watch_type, full_name, std::move(value));
    watched_variables_.emplace(watch_id_count_, w);
    return watch_id_count_++;
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
            return var.get_value_ptr();
        }
    }
    return nullptr;
}

std::string get_string_value(const std::optional<int64_t>& value) {
    std::string str_value;
    if (value) {
        str_value = std::to_string(*value);
    } else {
        str_value = Debugger::error_value_str;
    }
    return str_value;
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
                auto str_value = get_string_value(value);
                result.emplace_back(std::make_pair(watch_id, str_value));
                break;
            }
            case WatchType::data:
            case WatchType::changed: {
                // only if values are changed
                auto [changed, value] = var_changed(watch_id);
                if (changed) {
                    auto str_value = get_string_value(value);
                    result.emplace_back(std::make_pair(watch_id, str_value));
                }
                break;
            }
            case WatchType::delay_clock_edge: {
                // we assume this will be called every clock cycle
                auto new_value = get_value(watch_var.full_name);
                // we use the old value
                auto old_value_str = get_string_value(*watch_var.get_value());
                watch_var.set_value(new_value);
                result.emplace_back(std::make_pair(watch_id, old_value_str));
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
        auto const& watch_var_value = watch_var.get_value();
        if (!watch_var_value.has_value() || watch_var_value.value() != *value) changed = true;
        if (changed) {
            watch_var.set_value(value);
        }
        return {changed, value};
    }
    return {false, {}};
}

std::optional<int64_t> Monitor::WatchVariable::get_value() const { return *value_; }

void Monitor::WatchVariable::set_value(std::optional<int64_t> v) { *value_ = v; }

std::shared_ptr<std::optional<int64_t>> Monitor::WatchVariable::get_value_ptr() const {
    return value_;
}

Monitor::WatchVariable::WatchVariable(WatchType type, std::string full_name)
    : type(type),
      full_name(std::move(full_name)),
      value_(std::make_shared<std::optional<int64_t>>()) {}

Monitor::WatchVariable::WatchVariable(WatchType type, std::string full_name,
                                      std::shared_ptr<std::optional<int64_t>> v)
    : type(type), full_name(std::move(full_name)), value_(std::move(v)) {}

Monitor::WatchVariableBuffer::WatchVariableBuffer(WatchType type, std::string full_name,
                                                  uint32_t depth)
    : WatchVariable(type, std::move(full_name)), depth_(depth) {}

std::optional<int64_t> Monitor::WatchVariableBuffer::get_value() const {
    if (!values_.empty()) [[likely]] {
        return values_.front();
    } else {
        return std::nullopt;
    }
}

void Monitor::WatchVariableBuffer::set_value(std::optional<int64_t> v) {
    values_.emplace(v);
    if (values_.size() > depth_) {
        values_.pop();
    }
}

std::shared_ptr<std::optional<int64_t>> Monitor::WatchVariableBuffer::get_value_ptr() const {
    // buffer-based watch point will never use method
    return nullptr;
}

}  // namespace hgdb