#ifndef HGDB_TOOLS_VCD_DB_HH
#define HGDB_TOOLS_VCD_DB_HH

#include <fstream>
#include <memory>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>

#include "../waveform/waveform.hh"
#include "sqlite_orm/sqlite_orm.h"

namespace hgdb::vcd {

struct VCDDBSignal {
    uint64_t id;
    std::string name;
    std::unique_ptr<uint64_t> instance_id;
    uint32_t width;
};

struct VCDDBModule {
    uint64_t id;
    std::string name;
};

struct VCDDBValue {
    uint64_t id;
    std::unique_ptr<uint64_t> signal_id;
    uint64_t time;
    std::string value;
};

struct VCDDBModuleHierarchy {
    std::unique_ptr<uint64_t> parent_id;
    std::unique_ptr<uint64_t> child_id;
};

auto inline initial_vcd_db(const std::string &filename) {
    using namespace sqlite_orm;
    auto storage = make_storage(
        filename,
        make_table("module", make_column("id", &VCDDBModule::id, primary_key()),
                   make_column("name", &VCDDBModule::name)),
        make_table("signal", make_column("id", &VCDDBSignal::id, primary_key()),
                   make_column("name", &VCDDBSignal::name),
                   make_column("instance_id", &VCDDBSignal::instance_id),
                   make_column("width", &VCDDBSignal::width),
                   foreign_key(&VCDDBSignal::instance_id).references(&VCDDBModule::id)),
        make_table("value", make_column("id", &VCDDBValue::id, primary_key()),
                   make_column("signal_id", &VCDDBValue::signal_id),
                   make_column("time", &VCDDBValue::time), make_column("value", &VCDDBValue::value),
                   foreign_key(&VCDDBValue::signal_id).references(&VCDDBSignal::id)),
        make_table("hierarchy", make_column("parent_id", &VCDDBModuleHierarchy::parent_id),
                   make_column("child_id", &VCDDBModuleHierarchy::child_id),
                   foreign_key(&VCDDBModuleHierarchy::parent_id).references(&VCDDBModule::id),
                   foreign_key(&VCDDBModuleHierarchy::child_id).references(&VCDDBModule::id)));
    storage.sync_schema();
    return storage;
}

class VCDDatabase : public waveform::WaveformProvider {
public:
    explicit VCDDatabase(const std::string &filename) : VCDDatabase(filename, false) {}
    VCDDatabase(const std::string &filename, bool store_converted_db);
    std::optional<uint64_t> get_instance_id(const std::string &full_name) override;
    std::optional<uint64_t> get_signal_id(const std::string &full_name) override;
    std::vector<waveform::WaveformSignal> get_instance_signals(uint64_t instance_id) override;
    std::vector<waveform::WaveformInstance> get_child_instances(uint64_t instance_id) override;
    std::optional<waveform::WaveformSignal> get_signal(uint64_t signal_id) override;
    std::optional<std::string> get_instance(uint64_t instance_id) override;
    std::optional<std::string> get_signal_value(uint64_t id, uint64_t timestamp) override;
    std::string get_full_signal_name(uint64_t signal_id) override;
    std::string get_full_instance_name(uint64_t instance_id) override;
    std::optional<uint64_t> get_next_value_change_time(uint64_t signal_id,
                                                       uint64_t base_time) override;
    std::optional<uint64_t> get_prev_value_change_time(uint64_t signal_id, uint64_t base_time,
                                                       const std::string &target_value) override;
    std::pair<std::string, std::string> compute_instance_mapping(
        const std::unordered_set<std::string> &instance_names) override;

private:
    // parsing functions
    void parse_vcd(const std::string &filename);

    using VCDTable = decltype(initial_vcd_db(""));

    // storage
    void store_module(const std::string &name, uint64_t id);
    void store_hierarchy(uint64_t parent_id, uint64_t child_id);
    void store_signal(const std::string &name, uint64_t id, uint64_t parent_id, uint32_t width);
    void store_value(uint64_t id, uint64_t time, const std::string &value);
    std::unique_ptr<VCDTable> vcd_table_;

    // helper functions
    std::optional<uint64_t> match_hierarchy(const std::vector<std::string> &instance_tokens,
                                            std::vector<uint64_t> targets);
    static std::optional<std::string> get_vcd_db_filename(const std::string &filename, bool store);

    // used to uniquely identify the vcd value
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> vcd_values_;
    uint64_t vcd_value_count_ = 0;
};

}  // namespace hgdb::vcd

#endif  // HGDB_TOOLS_VCD_DB_HH
