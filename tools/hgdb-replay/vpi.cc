#include "vpi.hh"

#include <regex>

#include "../../src/util.hh"
#include "fmt/format.h"

namespace hgdb::replay {

ReplayVPIProvider::ReplayVPIProvider(std::unique_ptr<hgdb::vcd::VCDDatabase> db)
    : db_(std::move(db)) {
    // claim the nullptr;
    get_new_handle();
}

void ReplayVPIProvider::vpi_get_value(vpiHandle expr, p_vpi_value value_p) {
    // if there is an overridden value, use that
    // although it is unlikely (only clk signals)
    if (overridden_values_.find(expr) != overridden_values_.end()) [[unlikely]] {
        auto value = overridden_values_.at(expr);
        if (value_p->format == vpiIntVal) {
            value_p->value.integer = value;
        } else if (value_p->format == vpiHexStrVal) {
            str_buffer_ = fmt::format("{0:X}", value);
            value_p->value.str = const_cast<char *>(str_buffer_.c_str());
        }
        return;
    }
    if (signal_id_map_.find(expr) != signal_id_map_.end()) {
        auto signal_id = signal_id_map_.at(expr);
        auto value = db_->get_signal_value(signal_id, current_time_);
        if (value) {
            if (value_p->format == vpiIntVal) {
                // need to convert binary to actual integer values
                value_p->value.integer = convert_value(*value);
                return;
            } else if (value_p->format == vpiHexStrVal) {
                str_buffer_ = convert_str_value(*value);
                value_p->value.str = const_cast<char *>(str_buffer_.c_str());
                return;
            }
        }
    } else if (array_info_.find(expr) != array_info_.end()) {
        // need to slice out the information we need
        auto const &[parent_handle, slice_info] = array_info_.at(expr);
        if (signal_id_map_.find(parent_handle) != signal_id_map_.end()) {
            auto raw_value = db_->get_signal_value(signal_id_map_.at(parent_handle), current_time_);
            if (!raw_value) goto error;
            auto signal = db_->get_signal(signal_id_map_.at(parent_handle));
            auto array_size = array_map_.at(parent_handle).size();
            auto slice_size = signal->width / array_size;
            auto lo = slice_size * slice_info[0];
            auto hi = lo + slice_size;
            auto *handle = parent_handle;
            for (auto index = 1; index < slice_info.size(); index++) {
                auto width = hi - lo;
                array_size = array_map_.at(handle).size();
                slice_size = width / array_size;
                auto i = slice_info[index];
                lo = lo + slice_size * i;
                hi = lo + slice_size;
            }
            // need to slice out the raw value
            if (lo >= raw_value->size()) {
                goto error;
            }
            // need to slice out the raw_value
            hi = std::min(hi, raw_value->size());
            auto value = raw_value->substr(lo, hi - lo);
            if (value_p->format == vpiIntVal) {
                // need to convert binary to actual integer values
                value_p->value.integer = convert_value(value);
                return;
            } else if (value_p->format == vpiHexStrVal) {
                str_buffer_ = convert_str_value(value);
                value_p->value.str = const_cast<char *>(str_buffer_.c_str());
                return;
            }
        }
    }
error:
    value_p->value.integer = 0;
    value_p->value.str = nullptr;
}

PLI_INT32 ReplayVPIProvider::vpi_get(PLI_INT32 property, vpiHandle object) {
    if (property == vpiType) {
        if (signal_id_map_.find(object) != signal_id_map_.end()) {
            return vpiNet;
        }
        if (instance_id_map_.find(object) != instance_id_map_.end()) {
            return vpiModule;
        }
    } else if (property == vpiSize) {
        // don't care about vpi size since we can't read array size from VCD files
        // it has to be a signal
        if (signal_id_map_.find(object) == signal_id_map_.end()) {
            return vpiUndefined;
        }
        // query about the signal object
        auto signal_id = signal_id_map_.at(object);
        auto signal = db_->get_signal(signal_id);
        if (signal) {
            return signal->width;
        } else {
            return vpiUndefined;
        }
    }
    return vpiUndefined;
}

char *ReplayVPIProvider::vpi_get_str(PLI_INT32 property, vpiHandle object) {
    str_buffer_ = "";
    // we don't support def name since we can't read that from the vcd
    // client has to some other heuristics to figure out the hierarchy remapping
    if (property == vpiFullName) {
        if (signal_id_map_.find(object) != signal_id_map_.end()) {
            auto signal_id = signal_id_map_.at(object);
            str_buffer_ = db_->get_full_signal_name(signal_id);
        } else if (instance_id_map_.find(object) != instance_id_map_.end()) {
            auto instance_id = instance_id_map_.at(object);
            str_buffer_ = db_->get_full_instance_name(instance_id);
        }
    } else if (property == vpiName) {
        if (signal_id_map_.find(object) != signal_id_map_.end()) {
            auto signal_id = signal_id_map_.at(object);
            auto signal = db_->get_signal(signal_id);
            if (signal) {
                str_buffer_ = signal->name;
            }
        } else if (instance_id_map_.find(object) != instance_id_map_.end()) {
            auto instance_id = instance_id_map_.at(object);
            auto instance = db_->get_instance(instance_id);
            if (instance) {
                str_buffer_ = instance->name;
            }
        }
    }
    return const_cast<char *>(str_buffer_.c_str());
}

vpiHandle ReplayVPIProvider::vpi_handle_by_name(char *name, vpiHandle scope) {
    if (handle_mapping_.find(name) != handle_mapping_.end()) {
        return handle_mapping_.at(name);
    }
    // need to make sure it's either a signal or a module
    auto id = db_->get_instance_id(name);
    std::optional<vpiHandle> handle;
    if (id) {
        // it's a module
        handle = get_new_handle();
        instance_id_map_.emplace(*handle, *id);
    } else {
        // now try signal
        id = db_->get_signal_id(name);
        if (id) {
            // it's a signal
            handle = get_new_handle();
            signal_id_map_.emplace(*handle, *id);
        }
    }
    if (handle) {
        handle_mapping_.emplace(name, *handle);
        return *handle;
    } else {
        return nullptr;
    }
}

vpiHandle ReplayVPIProvider::vpi_scan(vpiHandle iterator) {
    if (scan_map_.find(iterator) != scan_map_.end()) {
        auto const &handles = scan_map_.at(iterator);
        auto idx = scan_iter_.at(iterator);
        if (idx >= handles.size()) {
            scan_map_.erase(iterator);
            scan_iter_.erase(iterator);
            return nullptr;
        }
        scan_iter_[iterator]++;
        auto *result = handles[idx];
        return result;
    } else {
        return nullptr;
    }
}

// NOLINTNEXTLINE
vpiHandle ReplayVPIProvider::vpi_iterate(PLI_INT32 type, vpiHandle refHandle) {
    std::vector<vpiHandle> handles;
    // need to create scan map
    if (type == vpiNet) {
        if (instance_id_map_.find(refHandle) != instance_id_map_.end()) {
            // need to find out all the signals within the module
            auto instance_id = instance_id_map_.at(refHandle);
            auto signals = db_->get_instance_signals(instance_id);
            // for each signal return a scan handle
            for (auto const &signal : signals) {
                auto *handle = get_signal_handle(signal.id);
                if (handle) {
                    handles.emplace_back(handle);
                }
            }
        }
    } else if (type == vpiModule) {
        if (refHandle == nullptr) {
            // this is top. top instance is always id 0 (how instance id allocation works)
            for (auto const &[handle, id] : instance_id_map_) {
                if (id == 0) {
                    handles.emplace_back(handle);
                    break;
                }
            }
        } else {
            if (instance_id_map_.find(refHandle) == instance_id_map_.end()) {
                return nullptr;
            }
            auto instance_id = instance_id_map_.at(refHandle);
            auto instances = db_->get_child_instances(instance_id);
            for (auto const &instance : instances) {
                auto *handle = get_instance_handle(instance.id);
                if (handle) {
                    handles.emplace_back(handle);
                }
            }
        }
    }
    if (!handles.empty()) {
        auto *iter = get_new_handle();
        for (auto const &handle : handles) {
            scan_map_[iter].emplace_back(handle);
        }
        scan_iter_.emplace(iter, 0);
        return iter;
    } else {
        return nullptr;
    }
}

PLI_INT32 ReplayVPIProvider::vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) {
    vlog_info_p->argc = static_cast<int>(argv_.size());
    vlog_info_p->argv = argv_.data();
    vlog_info_p->product = const_cast<char *>(product);
    vlog_info_p->version = const_cast<char *>(version);
    return 1;
}

void ReplayVPIProvider::vpi_get_time(vpiHandle object, p_vpi_time time_p) {
    if (time_p->type == vpiSimTime) {
        auto time = is_callback_eval_ ? current_time_ + 1 : current_time_;
        time_p->low = time & 0xFFFF'FFFF;
        time_p->high = time >> 32u;
    }
}

vpiHandle ReplayVPIProvider::vpi_register_cb(p_cb_data cb_data_p) {
    auto *handle = get_new_handle();
    callbacks_.emplace(handle, *cb_data_p);
    if (on_cb_added_) {
        (*on_cb_added_)(cb_data_p);
    }
    return handle;
}

PLI_INT32 ReplayVPIProvider::vpi_remove_cb(vpiHandle cb_obj) {
    if (callbacks_.find(cb_obj) != callbacks_.end()) {
        auto cb_struct = callbacks_.at(cb_obj);
        callbacks_.erase(cb_obj);
        if (on_cb_removed_) {
            (*on_cb_removed_)(cb_struct);
        }
        return 1;
    }
    return 0;
}

PLI_INT32 ReplayVPIProvider::vpi_release_handle(vpiHandle object) {
    if (signal_id_map_.find(object) != signal_id_map_.end()) {
        signal_id_map_.erase(object);
    } else if (instance_id_map_.find(object) != instance_id_map_.end()) {
        instance_id_map_.erase(object);
    }
    if (scan_iter_.find(object) != scan_iter_.end()) {
        scan_iter_.erase(object);
    }
    if (scan_map_.find(object) != scan_map_.end()) {
        scan_map_.erase(object);
    }
    return 1;
}

PLI_INT32 ReplayVPIProvider::vpi_control(PLI_INT32 operation, ...) {
    // TODO
    (void)(operation);
    return 1;
}

vpiHandle ReplayVPIProvider::vpi_handle_by_index(vpiHandle object, PLI_INT32 index) {
    if (array_map_.find(object) != array_map_.end()) {
        auto const &array = array_map_.at(object);
        if (array.size() > index) {
            return array[index];
        }
    }
    return nullptr;
}

bool ReplayVPIProvider::vpi_rewind(rewind_data *rewind_data) {
    if (on_rewound_) {
        return (*on_rewound_)(rewind_data);
    }
    return false;
}

void ReplayVPIProvider::set_argv(int argc, char **argv) {
    argv_str_.reserve(argc);
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        argv_str_.emplace_back(arg);
    }
    argv_.reserve(argv_str_.size());
    for (auto const &arg : argv_str_) {
        argv_.emplace_back(const_cast<char *>(arg.c_str()));
    }
}

void ReplayVPIProvider::set_on_cb_added(const std::function<void(p_cb_data)> &on_cb_added) {
    on_cb_added_ = on_cb_added;
}

void ReplayVPIProvider::set_on_cb_removed(
    const std::function<void(const s_cb_data &)> &on_cb_removed) {
    on_cb_removed_ = on_cb_removed;
}

void ReplayVPIProvider::set_on_reversed(const std::function<bool(rewind_data *)> &on_reversed) {
    on_rewound_ = on_reversed;
}

bool ReplayVPIProvider::is_valid_handle(const vpiHandle handle) const {
    if (signal_id_map_.find(handle) != signal_id_map_.end()) return true;
    if (instance_id_map_.find(handle) != instance_id_map_.end()) return true;
    if (scan_map_.find(handle) != scan_map_.end()) return true;
    if (scan_iter_.find(handle) != scan_iter_.end()) return true;
    return false;
}

void ReplayVPIProvider::trigger_cb(uint32_t reason, vpiHandle handle, int64_t value) {  // NOLINT
    // use a shallow copy to avoid problems where the callback removes from cb
    std::unordered_map<vpiHandle, s_cb_data> cb_copy = callbacks_;
    for (auto const &iter : cb_copy) {
        auto cb_data = iter.second;
        s_vpi_value vpi_value;
        if (cb_data.reason == reason) {
            if (reason == cbValueChange) {
                // only value change cares about the obj for now
                if (cb_data.obj != handle) {
                    // not the target
                    continue;
                } else {
                    cb_data.value = &vpi_value;
                    cb_data.value->value.integer = value;
                }
            }

            auto func = cb_data.cb_rtn;
            // vpi_value will be valid for this callback
            // after that it won't,
            func(&cb_data);
            // so set this to null
            cb_data.value = nullptr;
        }
    }
}

std::optional<uint64_t> ReplayVPIProvider::get_signal_id(vpiHandle handle) {
    if (signal_id_map_.find(handle) != signal_id_map_.end())
        return signal_id_map_.at(handle);
    else
        return std::nullopt;
}

void ReplayVPIProvider::add_overridden_value(vpiHandle handle, int64_t value) {
    overridden_values_.emplace(handle, value);
}

void ReplayVPIProvider::clear_overridden_values() { overridden_values_.clear(); }

int64_t ReplayVPIProvider::convert_value(const std::string &raw_value) {
    uint64_t bits = raw_value.size();
    int64_t result = 0;
    for (uint64_t i = 0; i < bits; i++) {
        auto bit = bits - i - 1;
        char v = raw_value[i];
        if (v == '1') {
            result |= 1 << bit;
        } else if (v == 'z' || v == 'x') {
            // invalid value we display 0, which is consistent with Verilator
            return 0;
        }
    }
    return result;
}

std::string ReplayVPIProvider::convert_str_value(const std::string &raw_value) {
    uint64_t bits = raw_value.size();
    std::string result;
    std::vector<char> buf;
    buf.reserve(5);

    auto de_buf = [&buf, &result]() {
        if (buf.empty()) return;
        char c;
        // figure out whether to put x or z in
        if (std::all_of(buf.begin(), buf.end(), [](const auto c) { return c == 'x'; })) {
            c = 'x';
        } else if (std::any_of(buf.begin(), buf.end(), [](const auto c) { return c == 'x'; })) {
            c = 'X';
        } else if (std::all_of(buf.begin(), buf.end(), [](const auto c) { return c == 'z'; })) {
            c = 'z';
        } else if (std::any_of(buf.begin(), buf.end(), [](const auto c) { return c == 'z'; })) {
            c = 'Z';
        } else {
            std::reverse(buf.begin(), buf.end());
            buf.emplace_back('\0');
            auto value = std::stoul(buf.data(), nullptr, 2);
            c = static_cast<char>(value >= 10 ? (value - 10) + 'A' : value + '0');
        }
        result = fmt::format("{0}{1}", c, result);
        buf.clear();
    };

    // we compute in reverse order
    for (uint64_t i = bits; i != 0; i--) {
        auto index = i - 1;
        if (buf.size() < 4) {
            buf.emplace_back(raw_value[index]);
        }
        if (buf.size() == 4) de_buf();
    }
    de_buf();

    return result;
}

void ReplayVPIProvider::build_array_table(const std::vector<std::string> &rtl_names) {
    // need to filter out the signal of interests
    std::set<std::string> array_signals;
    auto re = std::regex(R"([\.\[]\d+)", std::regex_constants::ECMAScript);
    for (auto const &name : rtl_names) {
        if (std::regex_search(name, re)) {
            array_signals.emplace(name);
        }
    }
    for (auto const &name : array_signals) {
        auto tokens = util::get_tokens(name, ".[]");
        // search backward to find the root signal
        // notice that we don't support generate construct since it's too much work to deal with
        std::optional<uint64_t> start_index;
        for (uint64_t index = tokens.size(); index != 0; index--) {
            auto i = index - 1;
            auto n = tokens[i];
            if (std::all_of(n.begin(), n.end(), isdigit)) {
                continue;
            }
            start_index = i;
            break;
        }
        if (!start_index) {
            // something went wrong?
            continue;
        }
        // found a non-digit root
        auto handle_name = util::join(tokens.begin(), tokens.begin() + *start_index + 1, ".");
        std::vector<uint64_t> slices;

        // see if we can find the handle name or not
        auto *handle = this->vpi_handle_by_name(const_cast<char *>(handle_name.c_str()), nullptr);
        if (!handle) {
            // could be unpacked array
            continue;
        }
        // fill out the array able
        auto *array_handle = handle;
        for (uint64_t idx = *start_index + 1; idx < tokens.size(); idx++) {
            auto index = std::stoul(tokens[idx]);
            auto &array_ = this->array_map_[array_handle];
            if (array_.size() < (index + 1)) {
                array_.resize(index + 1, nullptr);
            }
            array_handle = this->get_new_handle();
            array_[index] = array_handle;
            slices.emplace_back(index);
        }

        // put it into the array info
        this->array_info_.emplace(array_handle, std::make_pair(handle, slices));
        // reconstruct proper name
        std::string full_name = tokens[0];
        for (auto i = 1; i < tokens.size(); i++) {
            auto n = tokens[i];
            if (std::all_of(n.begin(), n.end(), isdigit)) {
                full_name.append(fmt::format("[{0}]", n));
            } else {
                full_name.append(fmt::format(".{0}", n));
            }
        }
        handle_mapping_.emplace(full_name, array_handle);
    }
}

vpiHandle ReplayVPIProvider::get_new_handle() {
    auto *p = vpi_handle_counter_++;
    return reinterpret_cast<uint32_t *>(p);
}

vpiHandle ReplayVPIProvider::get_instance_handle(uint64_t instance_id) const {
    // could use a bidirectional map to speed up
    for (auto const &[handle, id] : instance_id_map_) {
        if (id == instance_id) {
            return handle;
        }
    }
    return nullptr;
}

vpiHandle ReplayVPIProvider::get_signal_handle(uint64_t signal_id) const {
    for (auto const &[handle, id] : signal_id_map_) {
        if (id == signal_id) {
            return handle;
        }
    }
    return nullptr;
}

}  // namespace hgdb::replay