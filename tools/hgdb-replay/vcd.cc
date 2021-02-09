#include "vcd.hh"

#include <filesystem>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>

#include "fmt/format.h"
#include "log.hh"
#include "util.hh"

enum class sv { Date, Version, Timescale, Comment, Scope, Upscope, Var, Enddefinitions };

namespace hgdb::vcd {

VCDDatabase::VCDDatabase(const std::string &filename, bool store_converted_db) {
    // if not exists, don't do anything
    if (!std::filesystem::exists(filename)) return;

    auto vcd_filename = get_vcd_db_filename(filename, store_converted_db);
    // need to detect whether db exists before we actually create the file
    bool db_exists = vcd_filename && std::filesystem::exists(*vcd_filename);
    vcd_table_ = std::make_unique<VCDTable>(initial_vcd_db(vcd_filename ? *vcd_filename : ""));
    vcd_table_->sync_schema();
    if (db_exists) {
        // we're good
        return;
    }
    // set to off mode since we're not interested in recovery
    vcd_table_->pragma.journal_mode(sqlite_orm::journal_mode::OFF);

    std::ifstream stream(filename);
    if (stream.bad()) return;
    parse_vcd(stream);
}

std::optional<uint64_t> VCDDatabase::get_instance_id(const std::string &full_name) {
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
    auto module_id = get_instance_id(module_name);
    if (!module_id) return std::nullopt;
    auto vars = vcd_table_->select(&VCDSignal::id, where(c(&VCDSignal::instance_id) == *module_id &&
                                                         c(&VCDSignal::name) == tokens.back()));
    if (vars.size() != 1) return std::nullopt;
    return vars[0];
}

std::vector<VCDSignal> VCDDatabase::get_instance_signals(uint64_t instance_id) {
    using namespace sqlite_orm;
    auto vars = vcd_table_->get_all<VCDSignal>(where(c(&VCDSignal::instance_id) == instance_id));
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
    auto results = vcd_table_->select(
        &VCDValue::value, where(c(&VCDValue::signal_id) == id && c(&VCDValue::time) <= timestamp),
        order_by(&VCDValue::time).desc(), limit(1));
    if (results.empty())
        return std::nullopt;
    else
        return results[0];
}

std::string VCDDatabase::get_full_signal_name(uint64_t signal_id) {
    using namespace sqlite_orm;
    auto ptr = get_signal(signal_id);
    if (!ptr) return "";
    std::string name = ptr->name;
    uint64_t instance_id = *ptr->instance_id;
    name = fmt::format("{0}.{1}", get_full_instance_name(instance_id), name);
    return name;
}

std::string VCDDatabase::get_full_instance_name(uint64_t instance_id) {
    using namespace sqlite_orm;
    auto ptr = get_instance(instance_id);
    if (!ptr) return "";
    std::string name = ptr->name;
    while (true) {
        auto parents = vcd_table_->get_all<VCDModuleHierarchy>(
            where(c(&VCDModuleHierarchy::child_id) == instance_id));
        if (parents.empty()) break;
        instance_id = *(parents[0].parent_id);
        ptr = get_instance(instance_id);
        if (!ptr) break;
        name = fmt::format("{0}.{1}", ptr->name, name);
    }
    return name;
}

std::optional<uint64_t> VCDDatabase::get_next_value_change_time(uint64_t signal_id,
                                                                uint64_t base_time) {
    using namespace sqlite_orm;
    auto results = vcd_table_->select(
        &VCDValue::time,
        where(c(&VCDValue::signal_id) == signal_id && c(&VCDValue::time) > base_time),
        order_by(&VCDValue::time).asc(), limit(1));
    if (results.empty()) return std::nullopt;
    return results[0];
}

std::optional<uint64_t> VCDDatabase::get_prev_value_change_time(uint64_t signal_id,
                                                                uint64_t base_time,
                                                                const std::string &target_value) {
    using namespace sqlite_orm;
    auto results = vcd_table_->select(
        &VCDValue::time,
        where(c(&VCDValue::signal_id) == signal_id && c(&VCDValue::time) < base_time &&
              c(&VCDValue::value) == target_value),
        order_by(&VCDValue::time).desc(), limit(1));
    if (results.empty()) return std::nullopt;
    return results[0];
}

std::pair<std::string, std::string> VCDDatabase::compute_instance_mapping(
    const std::unordered_set<std::string> &instance_names) {
    // first get the longest instance name
    std::string instance_name;
    for (auto const &name : instance_names) {
        if (name.size() > instance_name.size()) {
            instance_name = name;
        }
    }

    // now trying to figure out the potential instances based on the tokenization
    auto tokens = util::get_tokens(instance_name, ".");
    std::optional<uint64_t> matched_instance_id;
    using namespace sqlite_orm;
    std::vector<uint64_t> targets =
        vcd_table_->select(&VCDModule::id, where(c(&VCDModule::name) == tokens.back()));
    if (tokens.size() == 1) {
        // we only have one level of hierarchy
        // trying our best
        auto instance_ids = vcd_table_->select(&VCDModuleHierarchy::child_id,
                                               where(c(&VCDModuleHierarchy::parent_id) == 0));
        if (!instance_ids.empty()) {
            uint64_t index = 0;
            if (instance_ids.size() > 1) {
                // best effort
                log::log(
                    log::log_level::error,
                    fmt::format("Unable to determine hierarchy for {0}. Using best effort strategy",
                                instance_name));
                // trying to find the one with the most amount of signals
                uint64_t count = 0;
                for (auto i = 0; i < instance_ids.size(); i++) {
                    auto signals = vcd_table_->count<VCDSignal>(
                        where(c(&VCDSignal::instance_id) == *instance_ids[i]));
                    if (signals > count) {
                        index = i;
                    }
                }
            }
            matched_instance_id = *(instance_ids[index]);
        }

    } else {
        matched_instance_id = match_hierarchy(tokens, targets);
    }
    if (!matched_instance_id) {
        // nothing we can do
        return {};
    }
    auto full_name = get_full_instance_name(*matched_instance_id);
    // find out at which point the become different
    uint64_t pos;
    auto full_name_tokens = util::get_tokens(full_name, ".");
    for (pos = 1; pos < tokens.size(); pos++) {
        if (tokens[tokens.size() - pos] == full_name_tokens[full_name_tokens.size() - pos])
            continue;
    }

    auto def_name = tokens[0];
    auto mapped_name =
        util::join(full_name_tokens.begin(),
                   full_name_tokens.begin() + (full_name_tokens.size() - pos + 1), ".") +
        ".";
    return {def_name, mapped_name};
}

void VCDDatabase::parse_vcd(std::istream &stream) {
    // the code below is mostly designed by Teguh Hofstee (https://github.com/hofstee)
    // heavily refactored to fit into hgdb's needs
    std::stack<uint64_t> scope;
    std::unordered_map<uint64_t, std::string> scope_name;
    std::unordered_map<std::string, uint64_t> var_mapping;
    uint64_t module_id_count = 0;
    uint64_t var_id_count = 0;

    // begin transaction
    vcd_table_->begin_transaction();

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
        auto token = next_token(stream);
        if (token.empty()) break;

        if (sv_map.find(token) == sv_map.end()) {
            printf("Unable to find token: %s\n", token.c_str());
        }

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
                next_token(stream);
                scope.pop();
                break;
            }
            case sv::Var: {
                parse_var_def(stream, scope, var_mapping, var_id_count);
                break;
            }
            case sv::Enddefinitions: {
                next_token(stream);
                parse_vcd_values(stream, var_mapping);
                break;
            }
            default: {
            }
        }
    }

    // end transaction
    vcd_table_->commit();
}
void VCDDatabase::parse_var_def(std::istream &stream, std::stack<uint64_t> &scope,
                                std::unordered_map<std::string, uint64_t> &var_mapping,
                                uint64_t &var_id_count) {
    next_token(stream);                           // type
    auto width = std::stoul(next_token(stream));  // width
    auto ident = next_token(stream);              // identifier
    auto name = next_token(stream);               // name

    auto temp = next_token(stream);
    if (temp != "$end") {
        // slice
        next_token(stream);
    }
    auto module_id = scope.top();
    var_mapping.emplace(ident, var_id_count);
    store_signal(name, var_id_count, module_id, width);
    var_id_count++;
}
void VCDDatabase::parse_module_def(std::istream &stream, std::stack<uint64_t> &scope,
                                   std::unordered_map<uint64_t, std::string> &scope_name,
                                   uint64_t &module_id_count) {
    next_token(stream);  // scope type
    auto name = next_token(stream);
    next_token(stream);
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

void VCDDatabase::store_signal(const std::string &name, uint64_t id, uint64_t parent_id,
                               uint32_t width) {
    VCDSignal signal{.id = id,
                     .name = name,
                     .instance_id = std::make_unique<uint64_t>(parent_id),
                     .width = width};
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
    auto &map = vcd_values_[id];
    uint64_t vcd_id;
    if (map.find(time) != map.end()) {
        // duplicated entry, replace it
        vcd_id = map.at(time);
    } else {
        vcd_id = vcd_value_count_++;
        map.emplace(time, vcd_id);
    }
    VCDValue v{
        .id = vcd_id, .signal_id = std::make_unique<uint64_t>(id), .time = time, .value = value};
    vcd_table_->replace(v);
}

std::optional<uint64_t> VCDDatabase::match_hierarchy(
    const std::vector<std::string> &instance_tokens, std::vector<uint64_t> targets) {
    if (instance_tokens.size() < 2) return std::nullopt;
    using namespace sqlite_orm;

    std::unordered_map<uint64_t, uint64_t> parent_mapping;

    // walking backwards
    for (uint64_t i = instance_tokens.size() - 2; i > 0; i--) {
        std::vector<uint64_t> result;
        for (auto const &id : targets) {
            auto instance_ids = vcd_table_->select(
                &VCDModule::id, where(c(&VCDModule::id) == &VCDModuleHierarchy::parent_id &&
                                      c(&VCDModuleHierarchy::child_id) == id &&
                                      c(&VCDModule::name) == instance_tokens[i]));
            for (auto const instance_id : instance_ids) {
                parent_mapping.emplace(instance_id, id);
            }
            result.insert(result.end(), instance_ids.begin(), instance_ids.end());
        }
        if (result.size() == 1) {
            // we have found it
            // matched id will be in the root
            auto id = result[0];
            while (parent_mapping.find(id) != parent_mapping.end()) {
                id = parent_mapping.at(id);
            }
            return id;
            break;
        } else {
            targets = result;
        }
    }
    return std::nullopt;
}

std::optional<std::string> VCDDatabase::get_vcd_db_filename(const std::string &filename,
                                                            bool store) {
    // check permission
    namespace fs = std::filesystem;
    auto name = filename + ".db";
    if (!store) {
        if (fs::exists(name)) {
            return name;
        } else {
            return std::nullopt;
        }
    }
    auto dirname = fs::path(filename).parent_path();
    auto perm = fs::status(filename).permissions();
    if ((perm & fs::perms::owner_write) != fs::perms::none) {
        // we can write to it
        return name;
    }
    return std::nullopt;
}

}  // namespace hgdb::vcd