#ifndef HGDB_TOOLS_VCD_HH
#define HGDB_TOOLS_VCD_HH

#include <fstream>
#include <memory>
#include <stack>
#include <string>
#include <vector>
#include <unordered_set>

#include "sqlite_orm/sqlite_orm.h"

namespace hgdb::vcd {

struct VCDSignal {
    uint64_t id;
    std::string name;
    std::unique_ptr<uint64_t> instance_id;
};

struct VCDModule {
    uint64_t id;
    std::string name;
};

struct VCDValue {
    std::unique_ptr<uint64_t> id;
    uint64_t time;
    std::string value;
};

struct VCDModuleHierarchy {
    std::unique_ptr<uint64_t> parent_id;
    std::unique_ptr<uint64_t> child_id;
};

auto inline initial_vcd_db(const std::string &filename) {
    using namespace sqlite_orm;
    auto storage = make_storage(
        filename,
        make_table("module", make_column("id", &VCDModule::id, primary_key()),
                   make_column("name", &VCDModule::name)),
        make_table("signal", make_column("id", &VCDSignal::id, primary_key()),
                   make_column("name", &VCDSignal::name),
                   make_column("instance_id", &VCDSignal::instance_id),
                   foreign_key(&VCDSignal::instance_id).references(&VCDModule::id)),
        make_table("value", make_column("id", &VCDValue::id), make_column("time", &VCDValue::time),
                   make_column("value", &VCDValue::value),
                   foreign_key(&VCDValue::id).references(&VCDSignal::id)),
        make_table("hierarchy", make_column("parent_id", &VCDModuleHierarchy::parent_id),
                   make_column("child_id", &VCDModuleHierarchy::child_id),
                   foreign_key(&VCDModuleHierarchy::parent_id).references(&VCDModule::id),
                   foreign_key(&VCDModuleHierarchy::child_id).references(&VCDModule::id)));
    storage.sync_schema();
    return storage;
}

class VCDDatabase {
public:
    explicit VCDDatabase(const std::string &filename);
    std::optional<uint64_t> get_instance_id(const std::string &full_name);
    std::optional<uint64_t> get_signal_id(const std::string &full_name);
    std::vector<VCDSignal> get_instance_signals(uint64_t instance_id);
    std::vector<VCDModule> get_child_instances(uint64_t instance_id);
    std::unique_ptr<VCDSignal> get_signal(uint64_t signal_id);
    std::unique_ptr<VCDModule> get_instance(uint64_t instance_id);
    std::optional<std::string> get_signal_value(uint64_t id, uint64_t timestamp);
    std::string get_full_signal_name(uint64_t signal_id);
    std::string get_full_instance_name(uint64_t instance_id);
    std::optional<uint64_t> get_next_value_change_time(uint64_t signal_id, uint64_t base_time);
    std::optional<uint64_t> get_prev_value_change_time(uint64_t signal_id, uint64_t base_time);
    std::pair<std::string, std::string> compute_instance_mapping(
        const std::unordered_set<std::string> &instance_names);

private:
    // parsing functions
    void parse_vcd(std::istream &stream);
    void parse_vcd_values(std::istream &stream,
                          const std::unordered_map<std::string, uint64_t> &var_mapping);
    void parse_module_def(std::istream &stream, std::stack<uint64_t> &scope,
                          std::unordered_map<uint64_t, std::string> &scope_name,
                          uint64_t &module_id_count);
    void parse_var_def(std::istream &stream, std::stack<uint64_t> &scope,
                       std::unordered_map<std::string, uint64_t> &var_mapping,
                       uint64_t &var_id_count);

    using VCDTable = decltype(initial_vcd_db(""));

    // tokenization
    static std::string next_token(std::istream &stream);

    // storage
    void store_module(const std::string &name, uint64_t id);
    void store_hierarchy(uint64_t parent_id, uint64_t child_id);
    void store_signal(const std::string &name, uint64_t id, uint64_t parent_id);
    void store_value(uint64_t id, uint64_t time, const std::string &value);
    std::unique_ptr<VCDTable> vcd_table_;

    // helper functions
    std::optional<uint64_t> match_hierarchy(const std::vector<std::string> &instance_tokens,
                                            std::vector<uint64_t> targets);
};

}  // namespace hgdb::vcd

#endif  // HGDB_TOOLS_VCD_HH
