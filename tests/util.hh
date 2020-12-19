#ifndef HGDB_UTIL_HH
#define HGDB_UTIL_HH

#include <unordered_map>

#include "gtest/gtest.h"
#include "rtl.hh"
#include "schema.hh"

class DBTestHelper : public ::testing::Test {
protected:
    void SetUp() override {
        auto db_filename = ":memory:";
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
            value_p->value.integer = signal_values_.at(expr);
        } else {
            value_p->value.integer = 0;
        }
    }

    PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) override {
        if (property == vpiType) {
            if (signals_.find(object) != signals_.end()) return vpiNet;
            if (modules_.find(object) != modules_.end()) return vpiModule;
        }
        return vpiError;
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
                str_buffer_ = name.at(pos);
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
            if (idx >= handles.size()) return nullptr;
            scan_iter_[iterator]++;
            auto result = handles[idx];
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
            }
            else {
                if (module_hierarchy_.find(refHandle) == module_hierarchy_.end()) return nullptr;
                handles = module_hierarchy_.at(refHandle);
            }
        }
        if (!handles.empty()) {
            auto iter = get_new_handle();
            auto result = iter;
            for (auto const &signal : handles) {
                scan_map_[iter].emplace_back(signal);
                iter = signal;
            }
            scan_iter_.emplace(result, 0);
            return result;
        }
        return nullptr;
    }

    vpiHandle get_new_handle() {
        auto p = vpi_handle_counter_++;
        return reinterpret_cast<uint32_t *>(p);
    }

    vpiHandle add_module(const std::string &def_name, const std::string &hierarchy_name) {
        auto handle = get_new_handle();
        modules_.emplace(handle, hierarchy_name);
        modules_defs_.emplace(handle, def_name);
        // compute the hierarchy automatically
        auto pos = hierarchy_name.find_last_of('.');
        if (pos != std::string::npos) {
            auto instance_name = hierarchy_name.substr(0, pos);
            auto parent = vpi_handle_by_name(const_cast<char *>(instance_name.c_str()), nullptr);
            if (!parent) throw std::runtime_error("unable to find parent of " + hierarchy_name);
            module_hierarchy_[parent].emplace(handle);
        }
        return handle;
    }

    vpiHandle add_signal(vpiHandle parent, const std::string &signal_name) {
        auto handle = get_new_handle();
        signals_.emplace(handle, signal_name);
        module_signals_[parent].emplace(handle);
        return handle;
    }

    void set_signal_value(vpiHandle handle, int64_t value) { signal_values_[handle] = value; }
    void set_top(vpiHandle top) { top_ = top; }

private:
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

    vpiHandle top_ = nullptr;
};

#endif  // HGDB_UTIL_HH
