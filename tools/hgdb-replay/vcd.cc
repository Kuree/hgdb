#include "vcd.hh"

#include <cassert>
#include <filesystem>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>

#include "log.hh"

enum class sv { Date, Version, Timescale, Comment, Scope, Upscope, Var, Enddefinitions };

// NOLINTNEXTLINE
static std::unordered_map<std::basic_string_view<char>, sv> sv_map = {
    {"$date", sv::Date},
    {"$version", sv::Version},
    {"$timescale", sv::Timescale},
    {"$comment", sv::Comment},
    {"$scope", sv::Scope},
    {"$upscope", sv::Upscope},
    {"$var", sv::Var},
    {"$enddefinitions", sv::Enddefinitions}};

namespace hgdb::vcd {

VCDDatabase::VCDDatabase(const std::string &filename) {
    vcd_table_ = std::make_unique<VCDTable>(initial_vcd_db(""));
    // if not exists, don't do anything
    if (!std::filesystem::exists(filename)) return;
    std::ifstream stream(filename);
    if (stream.bad()) return;
    parse_vcd(stream);
}

void VCDDatabase::parse_vcd(std::istream &stream) {
    // the code below is mostly designed by Teguh Hofstee (https://github.com/hofstee)
    // heavily refactored to fit into hgdb's needs
    std::stack<uint64_t> scope;
    std::unordered_map<uint64_t, std::string> scope_name;
    std::unordered_map<std::string, uint64_t> var_mapping;
    uint64_t module_id_count = 0;
    uint64_t var_id_count = 0;

    while (true) {
        auto token = next_token(stream);
        if (token.empty()) break;

        switch (sv_map.at(token)) {
            case sv::Date:
            case sv::Version:
            case sv::Timescale:
            case sv::Comment: {
                while (next_token(stream) != "$end")
                    ;
                break;
            }
            case sv::Scope: {
                parse_module_def(stream, scope, scope_name, module_id_count);
                break;
            }
            case sv::Upscope: {
                assert(next_token(stream) == "$end");
                scope.pop();
                break;
            }
            case sv::Var: {
                parse_var_def(stream, scope, var_mapping, var_id_count);
                break;
            }
            case sv::Enddefinitions: {
                assert(next_token(stream) == "$end");
                parse_vcd_values(stream, var_mapping);
                break;
            }
            default: {
            }
        }
    }
}
void VCDDatabase::parse_var_def(std::istream &stream, std::stack<uint64_t> &scope,
                                std::unordered_map<std::string, uint64_t> &var_mapping,
                                uint64_t &var_id_count) {
    next_token(stream);               // type
    next_token(stream);               // width
    auto ident = next_token(stream);  // identifier
    auto name = next_token(stream);   // name

    auto temp = next_token(stream);
    if (temp != "$end") {
        // slice
        assert(next_token(stream) == "$end");
    }
    auto module_id = scope.top();
    var_mapping.emplace(name, var_id_count);
    store_signal(name, var_id_count, module_id);
    var_id_count++;
}
void VCDDatabase::parse_module_def(std::istream &stream, std::stack<uint64_t> &scope,
                                   std::unordered_map<uint64_t, std::string> &scope_name,
                                   uint64_t &module_id_count) {
    next_token(stream);  // scope type
    auto name = next_token(stream);
    assert(next_token(stream) == "$end");
    std::optional<uint64_t> parent_id;
    if (!scope.empty()) {
        parent_id = scope.top();
    }

    scope_name.emplace(module_id_count, name);
    scope.emplace(module_id_count);
    store_module(name, module_id_count);
    if (parent_id) {
        store_hierarchy(*parent_id, module_id_count);
    }
    module_id_count++;
}

void VCDDatabase::parse_vcd_values(std::istream &stream,
                                   const std::unordered_map<std::string, uint64_t> &var_mapping) {
    size_t timestamp = 0;

    auto add_value = [this, &timestamp, &var_mapping](const std::string &identifier,
                                                      const std::string &value) {
        if (var_mapping.find(identifier) == var_mapping.end()) {
            hgdb::log::log(hgdb::log::log_level::error, "Unable to find identifier " + identifier);
            return;
        }
        auto var_id = var_mapping.at(identifier);
        store_value(var_id, timestamp, value);
    };

    while (true) {
        auto token = next_token(stream);
        if (token.empty()) break;

        if (token[0] == '#') {
            timestamp = std::stoi(std::string(token.substr(1)));
        } else if (std::string("01xz").find(token[0]) != std::string::npos) {
            auto value = std::string(1, token[0]);
            auto ident = std::string(token.substr(1));

            add_value(ident, value);

        } else if (std::string("b").find(token[0]) != std::string::npos) {
            auto value = std::string(token.substr(1));
            auto ident = std::string(next_token(stream));

            add_value(ident, value);
        } else if (token == "$dumpvars" || token == "$dumpall" || token == "$dumpon" ||
                   token == "$dumpoff") {
            while (true) {
                token = next_token(stream);
                if (token == "$end") {
                    break;
                } else if (std::string("01xz").find(token[0]) != std::string::npos) {
                    auto value = std::string(1, token[0]);
                    auto ident = std::string(token.substr(1));

                    add_value(ident, value);
                } else if (std::string("b").find(token[0]) != std::string::npos) {
                    auto value = std::string(token.substr(1));
                    auto ident = std::string(next_token(stream));

                    add_value(ident, value);
                }
            }
        }
    }
}

std::string VCDDatabase::next_token(std::istream &stream) {
    std::stringstream result;
    while (!stream.eof()) {
        char c;
        stream.get(c);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') continue;
        result << c;
    }

    return result.str();
}

void VCDDatabase::store_signal(const std::string &name, uint64_t id, uint64_t parent_id) {
    VCDSignal signal{.id = id, .name = name, .module_id = std::make_unique<uint64_t>(parent_id)};
    vcd_table_->insert(signal);
}

void VCDDatabase::store_hierarchy(uint64_t parent_id, uint64_t child_id) {
    VCDModuleHierarchy h{.parent_id = std::make_unique<uint64_t>(parent_id),
                         .child_id = std::make_unique<uint64_t>(child_id)};
    vcd_table_->insert(h);
}

void VCDDatabase::store_module(const std::string &name, uint64_t id) {
    VCDModule m{.id = id, .name = name};
    vcd_table_->insert(m);
}

void VCDDatabase::store_value(uint64_t id, uint64_t time, const std::string &value) {
    VCDValue v{.id = std::make_unique<uint64_t>(id), .time = time, .value = value};
    vcd_table_->insert(v);
}

}  // namespace hgdb::vcd