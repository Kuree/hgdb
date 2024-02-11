#ifndef HGDB_TOOLS_VCD_HH
#define HGDB_TOOLS_VCD_HH

#include <cstdint>
#include <fstream>
#include <functional>
#include <istream>
#include <optional>
#include <stack>
#include <string>
#include <unordered_set>

namespace hgdb::vcd {

struct VCSStreamInfo {
    uint64_t start_pos;
    uint64_t end_pos;
};

struct VCDScopeDef : public VCSStreamInfo {
    std::string name;
    std::string type;
};

struct VCDVarDef : public VCSStreamInfo {
    std::string identifier;
    std::string type;
    uint32_t width;
    std::string name;
    std::string slice;
};

struct VCDValue : public VCSStreamInfo {
    uint64_t time;
    std::string identifier;
    std::string value;
    bool is_event;
};

struct VCDMetaInfo : public VCSStreamInfo {
    enum class MetaType { date, version, timescale, comment };
    MetaType type;
    std::string value;
};

class VCDParser {
public:
    explicit VCDParser(const std::string &filename);
    explicit VCDParser(std::istream &stream) : stream_(&stream) {}
    bool parse();

    // set callback
    void set_on_meta_info(const std::function<void(const VCDMetaInfo &)> &func);
    void set_on_enter_scope(const std::function<void(const VCDScopeDef &)> &func);
    void set_exit_scope(const std::function<void()> &func);
    void set_value_change(const std::function<void(const VCDValue &)> &func);
    void set_on_var_def(const std::function<void(const VCDVarDef &)> &func);
    void set_on_definition_finished(const std::function<void()> &func);
    void set_on_time_change(const std::function<void(uint64_t)> &func);
    void set_on_dump_var_action(const std::function<void(const std::string &)> &func);

    const std::string &error_message() const { return error_message_; }
    bool has_error() const { return has_error_; }

    ~VCDParser();

private:
    std::istream *stream_;
    std::ifstream fstream_;
    std::string filename_;

    // callbacks
    std::optional<std::function<void(const VCDMetaInfo &)>> on_meta_info_;
    std::optional<std::function<void(const VCDScopeDef &)>> on_enter_scope_;
    std::optional<std::function<void()>> on_exit_scope_;
    std::optional<std::function<void(const VCDValue &)>> on_value_change_;
    std::optional<std::function<void(const VCDVarDef &)>> on_var_def_;
    std::optional<std::function<void(uint64_t)>> on_time_change_;
    std::optional<std::function<void(const std::string &)>> on_dump_var_action_;
    // parse stage notifier
    std::optional<std::function<void()>> on_definition_finished_;

    std::string error_message_;
    bool has_error_ = false;

    inline uint64_t tellg() { return stream_->tellg(); }

protected:
    // parser helpers
    void parse_meta(VCDMetaInfo::MetaType type);
    bool parse_scope_def();
    bool parse_var_def();
    bool parse_vcd_values();

    bool check_end(const std::string &token);
};

}  // namespace hgdb::vcd

#endif  // HGDB_TOOLS_VCD_HH
