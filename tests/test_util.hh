#ifndef HGDB_TEST_UTIL_HH
#define HGDB_TEST_UTIL_HH

#include <unordered_map>

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "rtl.hh"
#include "schema.hh"

class DBTestHelper : public ::testing::Test {
protected:
    void SetUp() override {
        const auto *db_filename = ":memory:";
        db = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(db_filename));
        db->sync_schema();
    }

    void TearDown() override { db.reset(); }
    std::unique_ptr<hgdb::DebugDatabase> db;
};

class MockVPIProvider : public hgdb::AVPIProvider {
public:
    MockVPIProvider() { get_new_handle(); }
    void vpi_get_value(vpiHandle expr, p_vpi_value value_p) override {
        if (signal_values_.find(expr) != signal_values_.end()) {
            if (value_p->format == vpiIntVal) {
                value_p->value.integer = signal_values_.at(expr);
            } else if (value_p->format == vpiHexStrVal) {
                str_buffer_ = fmt::format("{0:X}", signal_values_.at(expr));
                value_p->value.str = const_cast<char *>(str_buffer_.c_str());
            } else if (value_p->format == vpiBinStrVal) {
                str_buffer_ = fmt::format("{0:b}", signal_values_.at(expr));
                value_p->value.str = const_cast<char *>(str_buffer_.c_str());
            }
        } else {
            if (value_p->format == vpiIntVal) {
                value_p->value.integer = 0;
            } else if (value_p->format == vpiHexStrVal || value_p->format == vpiBinStrVal) {
                value_p->value.str = nullptr;
            }
        }
    }

    PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) override {
        if (property == vpiType) {
            if (signals_.find(object) != signals_.end()) return vpiNet;
            if (modules_.find(object) != modules_.end()) return vpiModule;
            // search for array
            for (auto const &iter : array_handles_) {
                if (std::find(iter.second.begin(), iter.second.end(), object) !=
                    iter.second.end()) {
                    return vpiReg;
                }
            }
        } else if (property == vpiSize) {
            // every signal is 32-bit for now
            // if it's clock object then it's 1
            // otherwise it's 32
            std::string name = this->vpi_get_str(vpiName, object);
            for (auto const &n : hgdb::RTLSimulatorClient::clock_names_) {
                if (name == n) return 1;
            }
            return 32;
        }
        return vpiUndefined;
    }

    char *vpi_get_str(PLI_INT32 property, vpiHandle object) override {
        str_buffer_ = "";
        if (property == vpiDefName) {
            if (modules_defs_.find(object) != modules_defs_.end()) {
                str_buffer_ = modules_defs_.at(object);
            }
        } else if (property == vpiFullName) {
            if (modules_.find(object) != modules_.end()) {
                str_buffer_ = modules_.at(object);
            }
        } else if (property == vpiName) {
            std::string name;
            if (modules_.find(object) != modules_.end()) name = modules_.at(object);
            if (signals_.find(object) != signals_.end()) name = signals_.at(object);
            if (!name.empty()) {
                auto pos = name.find_last_of('.');
                str_buffer_ = name.substr(pos + 1);
            }
        }
        return const_cast<char *>(str_buffer_.c_str());
    }

    vpiHandle vpi_handle_by_name(char *name, vpiHandle scope) override {
        for (auto const &iter : signals_) {
            if (iter.second == std::string(name)) {
                return iter.first;
            }
        }
        for (auto const &iter : modules_) {
            if (iter.second == std::string(name)) {
                return iter.first;
            }
        }
        return nullptr;
    }

    vpiHandle vpi_scan(vpiHandle iterator) override {
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

    vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) override {
        std::unordered_set<vpiHandle> handles;
        if (type == vpiNet) {
            // scan the signals_ inside the net
            // build up the scan map
            if (module_signals_.find(refHandle) == module_signals_.end()) return nullptr;
            handles = module_signals_.at(refHandle);
        } else if (type == vpiModule) {
            if (refHandle == nullptr) {
                handles.emplace(top_);
            } else {
                if (module_hierarchy_.find(refHandle) == module_hierarchy_.end()) return nullptr;
                handles = module_hierarchy_.at(refHandle);
            }
        }
        if (!handles.empty()) {
            auto *iter = get_new_handle();
            for (auto const &signal : handles) {
                scan_map_[iter].emplace_back(signal);
            }
            scan_iter_.emplace(iter, 0);
            return iter;
        }
        return nullptr;
    }

    PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p) override {
        vlog_info_p->argc = static_cast<int>(argv_.size());
        vlog_info_p->argv = argv_.data();
        vlog_info_p->product = const_cast<char *>(product);
        vlog_info_p->version = const_cast<char *>(version);
        return 1;
    }

    void vpi_get_time(vpiHandle object, p_vpi_time time_p) override {
        if (time_p->type == vpiSimTime) {
            time_p->low = time_ & 0xFFFF'FFFF;
            time_p->high = time_ >> 32u;
        }
    }

    vpiHandle vpi_register_cb(p_cb_data cb_data_p) override {
        auto *handle = get_new_handle();
        callbacks_.emplace(handle, *cb_data_p);
        return handle;
    }

    PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) override {
        if (callbacks_.find(cb_obj) != callbacks_.end()) {
            callbacks_.erase(cb_obj);
            return 1;
        }
        return 0;
    }

    // this is a noop since we don't actually allocate vpi Handle
    PLI_INT32 vpi_release_handle(vpiHandle) override { return 0; }

    PLI_INT32 vpi_control(PLI_INT32 operation, ...) override {
        vpi_ops_.emplace_back(operation);
        return 1;
    }

    vpiHandle vpi_handle_by_index(vpiHandle object, PLI_INT32 index) override {
        if (array_handles_.find(object) == array_handles_.end()) {
            return nullptr;
        }
        auto &array = array_handles_.at(object);
        if (index < array.size()) {
            return array[index];
        } else {
            return nullptr;
        }
    }

    vpiHandle get_new_handle() {
        auto *p = vpi_handle_counter_++;
        return reinterpret_cast<uint32_t *>(p);
    }

    uint64_t get_handle_count() { return reinterpret_cast<uint64_t>(vpi_handle_counter_); }

    vpiHandle add_module(const std::string &def_name, const std::string &hierarchy_name) {
        auto *handle = get_new_handle();
        modules_.emplace(handle, hierarchy_name);
        modules_defs_.emplace(handle, def_name);
        // compute the hierarchy automatically
        auto pos = hierarchy_name.find_last_of('.');
        if (pos != std::string::npos) {
            auto instance_name = hierarchy_name.substr(0, pos);
            auto *parent = vpi_handle_by_name(const_cast<char *>(instance_name.c_str()), nullptr);
            if (!parent) throw std::runtime_error("unable to find parent of " + hierarchy_name);
            module_hierarchy_[parent].emplace(handle);
        }
        return handle;
    }

    vpiHandle add_signal(vpiHandle parent, const std::string &signal_name) {
        auto *handle = get_new_handle();
        signals_.emplace(handle, signal_name);
        module_signals_[parent].emplace(handle);
        return handle;
    }

    std::vector<vpiHandle> set_signal_dim(vpiHandle signal, uint32_t dim) {
        array_handles_[signal].resize(dim);
        for (uint32_t i = 0; i < dim; i++) {
            array_handles_[signal][i] = get_new_handle();
        }
        return array_handles_[signal];
    }

    void set_argv(const std::vector<std::string> &argv) {
        argv_str_ = argv;
        argv_.reserve(argv.size());
        for (auto const &str : argv_str_) {
            argv_.emplace_back(const_cast<char *>(str.c_str()));
        }
    }

    void trigger_cb(uint32_t reason) {
        for (auto const &iter : callbacks_) {
            auto cb_data = iter.second;
            if (cb_data.reason == reason) {
                auto func = cb_data.cb_rtn;
                func(&cb_data);
            }
        }
    }

    std::vector<s_cb_data> get_cb_funcs(uint32_t reason) const {
        std::vector<s_cb_data> result;
        for (auto const &iter : callbacks_) {
            auto cb_data = iter.second;
            if (cb_data.reason == reason) {
                result.emplace_back(cb_data);
            }
        }
        return result;
    }

    void set_time(uint64_t time) { time_ = time; }

    void set_signal_value(vpiHandle handle, int64_t value) {
        bool value_changed = signal_values_.find(handle) == signal_values_.end() ||
                             signal_values_.at(handle) != value;
        signal_values_[handle] = value;
        // need to see if we need to call any callback
        // no adding callbacks inside the callback allowed
        if (value_changed) {
            for (auto &iter : callbacks_) {
                auto &cb = iter.second;
                if (cb.obj == handle && cb.reason == cbValueChange) {
                    // trigger the callback
                    cb.cb_rtn(&cb);
                }
            }
        }
    }
    void set_top(vpiHandle top) { top_ = top; }

    [[nodiscard]] const std::vector<uint32_t> &vpi_ops() const { return vpi_ops_; }

protected:
    std::string str_buffer_;
    char *vpi_handle_counter_ = nullptr;
    std::unordered_map<vpiHandle, std::vector<vpiHandle>> scan_map_;
    std::unordered_map<vpiHandle, uint64_t> scan_iter_;

    std::unordered_map<vpiHandle, std::string> signals_;
    std::unordered_map<vpiHandle, std::string> modules_;
    std::unordered_map<vpiHandle, std::string> modules_defs_;
    std::unordered_map<vpiHandle, std::unordered_set<vpiHandle>> module_hierarchy_;
    std::unordered_map<vpiHandle, std::unordered_set<vpiHandle>> module_signals_;
    std::unordered_map<vpiHandle, int64_t> signal_values_;
    // for arrays
    std::unordered_map<vpiHandle, std::vector<vpiHandle>> array_handles_;

    std::unordered_map<vpiHandle, s_cb_data> callbacks_;

    std::vector<uint32_t> vpi_ops_;

    std::vector<std::string> argv_str_;
    std::vector<char *> argv_;
    uint64_t time_ = 0;

    vpiHandle top_ = nullptr;

public:
    constexpr static auto product = "RTLMock";
    constexpr static auto version = "0.1";
};

#endif  // HGDB_TEST_UTIL_HH
