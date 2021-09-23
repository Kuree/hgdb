#ifndef HGDB_WAVEFORM_HH
#define HGDB_WAVEFORM_HH
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

struct WaveformSignal {
    uint64_t id;
    std::string name;
    uint32_t width;
};

struct WaveformInstance {
    uint64_t id;
    std::string name;
};

namespace hgdb::waveform {
class WaveformProvider {
public:
    virtual std::optional<uint64_t> get_instance_id(const std::string &full_name) = 0;
    virtual std::optional<uint64_t> get_signal_id(const std::string &full_name) = 0;
    virtual std::vector<WaveformSignal> get_instance_signals(uint64_t instance_id) = 0;
    virtual std::vector<WaveformInstance> get_child_instances(uint64_t instance_id) = 0;
    virtual std::optional<WaveformSignal> get_signal(uint64_t signal_id) = 0;
    virtual std::optional<std::string> get_instance(uint64_t instance_id) = 0;
    virtual std::optional<std::string> get_signal_value(uint64_t id, uint64_t timestamp) = 0;
    virtual std::string get_full_signal_name(uint64_t signal_id) = 0;
    virtual std::string get_full_instance_name(uint64_t instance_id) = 0;
    virtual std::optional<uint64_t> get_next_value_change_time(uint64_t signal_id,
                                                               uint64_t base_time) = 0;
    virtual std::optional<uint64_t> get_prev_value_change_time(uint64_t signal_id,
                                                               uint64_t base_time,
                                                               const std::string &target_value) = 0;
    inline virtual std::pair<std::string, std::string> compute_instance_mapping(
        const std::unordered_set<std::string> &instance_names) {
        return {};
    }
    [[nodiscard]] inline virtual bool has_inst_definition() const { return false; }
    [[nodiscard]] virtual std::optional<std::string> get_instance_definition(
        uint64_t instance_id) const {
        return std::nullopt;
    }

    [[nodiscard]] inline virtual uint64_t top_instance_id() const { return 0; }

    virtual ~WaveformProvider() = default;
};
}  // namespace hgdb::waveform

#endif  // HGDB_WAVEFORM_HH
