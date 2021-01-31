#include "vcd.hh"

#include <cassert>
#include <filesystem>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>

#include "log.hh"
#include "util.hh"

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
    vcd_table_->sync_schema();
    // if not exists, don't do anything
    if (!std::filesystem::exists(filename)) return;
    std::ifstream stream(filename);
    if (stream.bad()) return;
    parse_vcd(stream);
}

std::optional<uint64_t> VCDDatabase::get_module_id(const std::string &full_name) {
    using namespace sqlite_orm;
    // notice that SQLite doesn't support recursive query
    auto tokens = util::get_tokens(full_name, ".");
    // find the top level first
    auto tops = vcd_table_->select(&VCDModule::id, where(c(&VCDModule::name) == tokens[0]));
    if (tops.empty()) return std::nullopt;
    auto parent_id = tops[0];
    for (uint64_t i = 1; i < tokens.size(); i++) {
        auto child_name = tokens[i];
        auto ins = vcd_table_->select(&VCDModule::id,
                                      where(c(&VCDModuleHierarchy::parent_id) == parent_id &&
                                            c(&VCDModuleHierarchy::child_id) == &VCDModule::id &&
                                            c(&VCDModule::name) == child_name));
        if (ins.size() != 1) return std::nullopt;
        parent_id = ins[0];
    }
    return parent_id;
}

std::optional<uint64_t> VCDDatabase::get_signal_id(const std::string &full_name) {
    // notice that this does not work with packed struct, which is fine since packed struct
    // are merged into a single bus anyway. in this case, we can't read out the individual
    // data anyway
    using namespace sqlite_orm;
    auto tokens = util::get_tokens(full_name, ".");
    if (tokens.size() < 2) return std::nullopt;
    // the last one is the signal name
    auto module_name = util::join(tokens.begin(), tokens.begin() + tokens.size() - 1, ".");
    auto module_id = get_module_id(module_name);
    if (!module_id) return std::nullopt;
    auto vars = vcd_table_->select(&VCDSignal::id, where(c(&VCDSignal::module_id) == *module_id &&
                                                         c(&VCDSignal::name) == tokens.back()));
    if (vars.size() != 1) return std::nullopt;
    return vars[0];
}

std::vector<VCDSignal> VCDDatabase::get_instance_signals(uint64_t instance_id) {
    using namespace sqlite_orm;
    auto vars = vcd_table_->get_all<VCDSignal>(where(c(&VCDSignal::module_id) == instance_id));
    return vars;
}

std::vector<VCDModule> VCDDatabase::get_child_instances(uint64_t instance_id) {
    using namespace sqlite_orm;
    auto vars = vcd_table_->select(columns(&VCDModule::name, &VCDModule::id),
                                   where(c(&VCDModuleHierarchy::parent_id) == instance_id &&
                                         c(&VCDModuleHierarchy::child_id) == &VCDModule::id));
    std::vector<VCDModule> result;
    result.reserve(vars.size());
    for (auto const &[name, id] : vars) {
        result.emplace_back(VCDModule{.id = id, .name = name});
    }
    return result;
}

std::unique_ptr<VCDSignal> VCDDatabase::get_signal(uint64_t signal_id) {
    return std::move(vcd_table_->get_pointer<VCDSignal>(signal_id));
}

std::unique_ptr<VCDModule> VCDDatabase::get_instance(uint64_t instance_id) {
    return std::move(vcd_table_->get_pointer<VCDModule>(instance_id));
}

std::optional<std::string> VCDDatabase::get_signal_value(uint64_t id, uint64_t timestamp) {
    // sort timestamp and use the maximum timestamp that's < timestamp
    // this utilize some nice SQL language features. It will be as fast as sqlite can process
    using namespace sqlite_orm;
    auto results = vcd_table_->get_all<VCDValue>(
        where(c(&VCDValue::id) == id && c(&VCDValue::time) <= timestamp),
        order_by(&VCDValue::time).desc(), limit(1));
    if (results.empty())
        return std::nullopt;
    else
        return results[0].value;
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
    var_mapping.emplace(ident, var_id_count);
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
    uint64_t length = 0;
    while (!stream.eof()) {
        char c;
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

void VCDDatabase::store_signal(const std::string &name, uint64_t id, uint64_t parent_id) {
    VCDSignal signal{.id = id, .name = name, .module_id = std::make_unique<uint64_t>(parent_id)};
    vcd_table_->replace(signal);
}

void VCDDatabase::store_hierarchy(uint64_t parent_id, uint64_t child_id) {
    VCDModuleHierarchy h{.parent_id = std::make_unique<uint64_t>(parent_id),
                         .child_id = std::make_unique<uint64_t>(child_id)};
    vcd_table_->replace(h);
}

void VCDDatabase::store_module(const std::string &name, uint64_t id) {
    VCDModule m{.id = id, .name = name};
    vcd_table_->replace(m);
}

void VCDDatabase::store_value(uint64_t id, uint64_t time, const std::string &value) {
    VCDValue v{.id = std::make_unique<uint64_t>(id), .time = time, .value = value};
    vcd_table_->replace(v);
}

}  // namespace hgdb::vcd