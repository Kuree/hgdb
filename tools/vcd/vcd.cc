#include "vcd.hh"

#include <sstream>
#include <filesystem>

namespace hgdb::vcd {

enum class sv { Date, Version, Timescale, Comment, Scope, Upscope, Var, Enddefinitions };

constexpr auto *end_str = "$end";

VCDParser::VCDParser(const std::string &filename) : filename_(filename) {
    stream_ = std::ifstream(filename);
    if (!std::filesystem::exists(filename)) {
        error_message_ = "Unable to open " + filename_;
        has_error_ = true;
    }
}

std::string next_token(std::istream &stream) {
    if (stream.bad()) [[unlikely]]
        return {};
    std::stringstream result;
    uint64_t length = 0;
    while (!stream.eof()) {
        char c = '\0';
        stream.get(c);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') {
            if (length == 0)
                continue;
            else
                break;
        }
        result << c;
        length++;
    }

    return result.str();
}

bool VCDParser::parse() {  // NOLINT
    if (has_error_) return false;
    // the code below is mostly designed by Teguh Hofstee (https://github.com/hofstee)
    // heavily refactored to fit into hgdb's needs

    static const std::unordered_map<std::basic_string_view<char>, sv> sv_map = {
        {"$date", sv::Date},
        {"$version", sv::Version},
        {"$timescale", sv::Timescale},
        {"$comment", sv::Comment},
        {"$scope", sv::Scope},
        {"$upscope", sv::Upscope},
        {"$var", sv::Var},
        {"$enddefinitions", sv::Enddefinitions}};

    while (true) {
        auto token = next_token(stream_);
        if (token.empty()) return true;

        if (sv_map.find(token) == sv_map.end()) {
            error_message_ = "Unable to find token: " + token;
            return false;
        }

        static const std::unordered_map<sv, VCDMetaInfo::MetaType> meta_map = {
            {sv::Date, VCDMetaInfo::MetaType::date},
            {sv::Version, VCDMetaInfo::MetaType::version},
            {sv::Timescale, VCDMetaInfo::MetaType::timescale},
            {sv::Comment, VCDMetaInfo::MetaType::comment}};

        auto token_type = sv_map.at(token);
        switch (token_type) {
            case sv::Date:
            case sv::Version:
            case sv::Timescale:
            case sv::Comment: {
                parse_meta(meta_map.at(token_type));
                break;
            }
            case sv::Scope: {
                if (!parse_scope_def()) return false;
                break;
            }
            case sv::Upscope: {
                token = next_token(stream_);
                if (!check_end(token)) return false;
                if (on_exit_scope_) (*on_exit_scope_)();
                break;
            }
            case sv::Var: {
                if (!parse_var_def()) return false;
                break;
            }
            case sv::Enddefinitions: {
                token = next_token(stream_);
                if (!check_end(token)) return false;
                if (on_definition_finished_) (*on_definition_finished_)();
                if (!parse_vcd_values()) return false;
                break;
            }
            default: {
            }
        }
    }

    return true;
}

void VCDParser::set_exit_scope(const std::function<void()> &func) { on_exit_scope_ = func; }

void VCDParser::set_on_enter_scope(const std::function<void(const VCDScopeDef &)> &func) {
    on_enter_scope_ = func;
}

void VCDParser::set_on_meta_info(const std::function<void(const VCDMetaInfo &)> &func) {
    on_meta_info_ = func;
}

void VCDParser::set_value_change(const std::function<void(const VCDValue &)> &func) {
    on_value_change_ = func;
}

void VCDParser::set_on_var_def(const std::function<void(const VCDVarDef &)> &func) {
    on_var_def_ = func;
}

void VCDParser::set_on_definition_finished(const std::function<void()> &func) {
    on_definition_finished_ = func;
}

void VCDParser::set_on_time_change(const std::function<void(uint64_t)> &func) {
    on_time_change_ = func;
}

void VCDParser::set_on_dump_var_action(const std::function<void(const std::string &)> &func) {
    on_dump_var_action_ = func;
}

VCDParser::~VCDParser() {
    // close the stream_
    if (!stream_.bad()) {
        stream_.close();
    }
}

void VCDParser::parse_meta(VCDMetaInfo::MetaType type) {
    std::stringstream ss;
    std::string token;

    while ((token = next_token(stream_)) != end_str) {
        ss << " " << token;
    }
    if (on_meta_info_) {
        VCDMetaInfo meta_info{.type = type, .value = ss.str()};
        (*on_meta_info_)(meta_info);
    }
}

bool VCDParser::parse_scope_def() {
    static VCDScopeDef scope_def;
    scope_def.type = next_token(stream_);  // scope type
    scope_def.name = next_token(stream_);
    auto token = next_token(stream_);
    if (token != end_str) {
        error_message_ = "Illegal VCD file";
        return false;
    }
    if (on_enter_scope_) {
        (*on_enter_scope_)(scope_def);
    }
    return true;
}

bool VCDParser::parse_var_def() {
    static VCDVarDef var_def;

    var_def.type = next_token(stream_);
    var_def.width = std::stoul(next_token(stream_));
    var_def.identifier = next_token(stream_);
    var_def.name = next_token(stream_);

    auto token = next_token(stream_);
    if (token != end_str) {
        // slice
        var_def.slice = token;
        token = next_token(stream_);
        if (token != end_str) {
            return false;
        }
    } else {
        var_def.slice = "";
    }

    if (on_var_def_) {
        (*on_var_def_)(var_def);
    }
    return true;
}

bool VCDParser::parse_vcd_values() {  // NOLINT
    size_t timestamp = 0;

    auto add_value = [this, &timestamp](const std::string &identifier, const std::string &value,
                                        bool is_event = false) {
        VCDValue v{
            .time = timestamp, .identifier = identifier, .value = value, .is_event = is_event};
        if (on_value_change_) {
            (*on_value_change_)(v);
        }
    };

    while (true) {
        auto token = next_token(stream_);
        if (token.empty()) return true;

        if (token[0] == '#') {
            timestamp = std::stoi(std::string(token.substr(1)));
            if (on_time_change_) (*on_time_change_)(timestamp);
        } else if (std::string("01xz").find(token[0]) != std::string::npos) {
            auto value = std::string(1, token[0]);
            auto ident = std::string(token.substr(1));

            add_value(ident, value, true);
        } else if (std::string("b").find(token[0]) != std::string::npos) {
            auto value = std::string(token.substr(1));
            auto ident = std::string(next_token(stream_));

            add_value(ident, value);
        } else if (token == "$dumpvars" || token == "$dumpall" || token == "$dumpon" ||
                   token == "$dumpoff") {
            if (on_dump_var_action_) (*on_dump_var_action_)(token);
            while (true) {
                token = next_token(stream_);
                if (token == "$end") {
                    break;
                } else if (std::string("01xz").find(token[0]) != std::string::npos) {
                    auto value = std::string(1, token[0]);
                    auto ident = std::string(token.substr(1));

                    add_value(ident, value, true);
                } else if (std::string("b").find(token[0]) != std::string::npos) {
                    auto value = std::string(token.substr(1));
                    auto ident = std::string(next_token(stream_));

                    add_value(ident, value);
                }
            }
        }
    }
}

bool VCDParser::check_end(const std::string &token) {
    if (token != end_str) {
        std::stringstream ss;
        ss << "Illegal VCD file " << filename_ << std::endl;
        ss << "  Missing " << end_str;
        error_message_ = ss.str();
        return false;
    }
    return true;
}

}  // namespace hgdb::vcd