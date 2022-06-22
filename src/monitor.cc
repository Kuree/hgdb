#include "monitor.hh"

namespace hgdb {
Monitor::Monitor() {
    // everything is 0 if not set up
    get_value = [](vpiHandle) { return 0; };
    get_handle = [](const std::string&) { return nullptr; };
}

Monitor::Monitor(std::function<std::optional<int64_t>(vpiHandle)> get_value,
                 std::function<vpiHandle(const std::string&)> get_handle)
    : get_value(std::move(get_value)), get_handle(std::move(get_handle)) {}

uint64_t Monitor::add_monitor_variable(const std::string& full_name, WatchType watch_type) {
    // we assume full name is checked already
    // need to search if we have the same handle already
    auto* handle = get_handle(full_name);
    auto watched = is_monitored(handle, watch_type);
    if (watched) {
        return *watched;
    }
    auto w = std::make_unique<WatchVariable>(watch_type, full_name, handle);
    return add_watch_var(std::move(w));
}

uint64_t Monitor::add_monitor_variable(const std::string& full_name, WatchType watch_type,
                                       std::shared_ptr<std::optional<int64_t>> value) {
    auto* handle = get_handle(full_name);
    auto watched = is_monitored(handle, watch_type);
    if (watched) {
        return *watched;
    }
    auto w = std::make_unique<WatchVariable>(watch_type, full_name, handle, std::move(value));
    return add_watch_var(std::move(w));
}

uint64_t Monitor::add_monitor_variable(const std::string& full_name, uint32_t depth,
                                       std::optional<int64_t> v) {
    // for now, no existing check?
    auto* handle = get_handle(full_name);
    auto w = std::make_unique<WatchVariableBuffer>(full_name, handle, depth);
    w->set_value(v);
    return add_watch_var(std::move(w));
}

void Monitor::remove_monitor_variable(uint64_t watch_id) {
    if (watched_variables_.find(watch_id) != watched_variables_.end()) {
        watched_variables_.erase(watch_id);
    }
}

void Monitor::set_monitor_variable_condition(uint64_t id, std::function<bool()> cond) {
    if (watched_variables_.find(id) != watched_variables_.end()) [[likely]] {
        watched_variables_.at(id)->enable_cond = std::move(cond);
    }
}

std::optional<uint64_t> Monitor::is_monitored(vpiHandle handle, WatchType watch_type) const {
    for (auto const& [id, var] : watched_variables_) {
        if (var->handle == handle && var->type == watch_type) [[unlikely]] {
            // reuse the existing ID
            return id;
        }
    }
    return std::nullopt;
}

std::shared_ptr<std::optional<int64_t>> Monitor::get_watched_value_ptr(
    const std::unordered_set<std::string>& var_names, WatchType type) const {
    for (auto const& [id, var] : watched_variables_) {
        if (var_names.find(var->full_name) != var_names.end() && var->type == type) {
            // reuse the existing ID
            return var->get_value_ptr();
        }
    }
    return nullptr;
}

std::vector<std::pair<uint64_t, std::optional<int64_t>>> Monitor::get_watched_values(
    WatchType type) {
    std::vector<std::pair<uint64_t, std::optional<int64_t>>> result;
    // this is the maximum size
    result.reserve(watched_variables_.size());

    for (auto& [watch_id, watch_var] : watched_variables_) {
        if (watch_var->type != type) continue;
        switch (watch_var->type) {
            case WatchType::breakpoint:
            case WatchType::clock_edge: {
                std::optional<int64_t> value;
                if (!watch_var->enable_cond || (*watch_var->enable_cond)()) {
                    value = get_value(watch_var->handle);
                } else {
                    value = watch_var->get_value();
                }
                result.emplace_back(std::make_pair(watch_id, value));
                break;
            }
            case WatchType::data:
            case WatchType::changed: {
                // only if values are changed
                auto [changed, value] = var_changed(watch_id);
                if (changed) {
                    result.emplace_back(std::make_pair(watch_id, value));
                }
                break;
            }
            case WatchType::delay_clock_edge: {
                // we assume this will be called every clock cycle
                auto new_value = get_value(watch_var->handle);
                // we use the old value
                auto old_value = *watch_var->get_value();
                watch_var->set_value(new_value);
                result.emplace_back(std::make_pair(watch_id, old_value));
            }
        }
    }

    return result;
}

uint64_t Monitor::num_watches(const std::string& name, WatchType type) const {
    uint64_t result = 0;
    for (auto const& iter : watched_variables_) {
        if (iter.second->full_name == name && iter.second->type == type) {
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
    auto value = get_value(watch_var->handle);
    if (value) {
        bool changed = false;
        auto const& watch_var_value = watch_var->get_value();
        if (!watch_var_value.has_value() || watch_var_value.value() != *value) changed = true;
        if (changed) {
            watch_var->set_value(value);
        }
        return {changed, value};
    }
    return {false, {}};
}

uint32_t Monitor::add_watch_var(std::unique_ptr<WatchVariable> w) {
    watched_variables_.emplace(watch_id_count_, std::move(w));
    return watch_id_count_++;
}

std::optional<int64_t> Monitor::WatchVariable::get_value() const { return *value_; }

void Monitor::WatchVariable::set_value(std::optional<int64_t> v) { *value_ = v; }

std::shared_ptr<std::optional<int64_t>> Monitor::WatchVariable::get_value_ptr() const {
    return value_;
}

Monitor::WatchVariable::WatchVariable(WatchType type, std::string full_name, vpiHandle handle)
    : type(type),
      full_name(std::move(full_name)),
      handle(handle),
      value_(std::make_shared<std::optional<int64_t>>()) {}

Monitor::WatchVariable::WatchVariable(WatchType type, std::string full_name, vpiHandle handle,
                                      std::shared_ptr<std::optional<int64_t>> v)
    : type(type), full_name(std::move(full_name)), handle(handle), value_(std::move(v)) {}

Monitor::WatchVariableBuffer::WatchVariableBuffer(std::string full_name, vpiHandle handle,
                                                  uint32_t depth)
    : WatchVariable(WatchType::delay_clock_edge, std::move(full_name), handle), depth_(depth) {}

std::optional<int64_t> Monitor::WatchVariableBuffer::get_value() const {
    if (values_.size() == depth_) [[likely]] {
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