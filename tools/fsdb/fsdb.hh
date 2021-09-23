#ifndef HGDB_FSDB_HH
#define HGDB_FSDB_HH

#include <unordered_map>

#include "../waveform/waveform.hh"

class ffrObject;

namespace hgdb::fsdb {

class FSDBProvider : public hgdb::waveform::WaveformProvider {
public:
    explicit FSDBProvider(const std::string &filename);
    std::optional<uint64_t> get_instance_id(const std::string &full_name) override;
    std::optional<uint64_t> get_signal_id(const std::string &full_name) override;
    std::vector<WaveformSignal> get_instance_signals(uint64_t instance_id) override;
    std::vector<WaveformInstance> get_child_instances(uint64_t instance_id) override;
    std::optional<WaveformSignal> get_signal(uint64_t signal_id) override;
    std::optional<std::string> get_instance(uint64_t instance_id) override;
    std::optional<std::string> get_signal_value(uint64_t id, uint64_t timestamp) override;
    std::string get_full_signal_name(uint64_t signal_id) override;
    std::string get_full_instance_name(uint64_t instance_id) override;
    std::optional<uint64_t> get_next_value_change_time(uint64_t signal_id,
                                                       uint64_t base_time) override;
    std::optional<uint64_t> get_prev_value_change_time(uint64_t signal_id, uint64_t base_time,
                                                       const std::string &target_value) override;

    [[nodiscard]] bool has_inst_definition() const override { return true; }
    std::optional<std::string> get_instance_definition(uint64_t instance_id) const override;
    uint64_t top_instance_id() const override { return top_instance_ ; }

    ~FSDBProvider() override;

private:
    ffrObject *fsdb_ = nullptr;

    // mapping information
    std::unordered_map<uint64_t, WaveformInstance> instance_map_;
    // for fast look up
    std::unordered_map<std::string, uint64_t> instance_name_map_;
    std::unordered_map<uint64_t, WaveformSignal> variable_map_;
    std::unordered_map<std::string, uint64_t> variable_id_map_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> instance_vars_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> instance_hierarchy_;
    std::unordered_map<std::string, std::unordered_set<uint64_t>> instance_def_map_;
    uint64_t top_instance_ = 0;
};

}  // namespace hgdb::fsdb

#endif  // HGDB_FSDB_HH
