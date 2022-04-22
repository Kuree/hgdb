#include "db.hh"

#include <filesystem>
#include <regex>
#include <unordered_set>

#include "fmt/format.h"
#include "jschema.hh"
#include "log.hh"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "util.hh"
#include "valijson/adapters/rapidjson_adapter.hpp"
#include "valijson/schema.hpp"
#include "valijson/schema_parser.hpp"
#include "valijson/validator.hpp"

namespace hgdb {

DBSymbolTableProvider::DBSymbolTableProvider(const std::string &filename) {
    db_ = std::make_unique<DebugDatabase>(init_debug_db(filename));
    db_->sync_schema();

    compute_use_base_name();
}

DBSymbolTableProvider::DBSymbolTableProvider(std::unique_ptr<DebugDatabase> db) {
    // this will transfer ownership
    db_ = std::move(db);
    compute_use_base_name();
}

void DBSymbolTableProvider::close() {
    if (!is_closed_) [[likely]] {
        db_.reset();
        is_closed_ = true;
    }
}

std::vector<BreakPoint> DBSymbolTableProvider::get_breakpoints(const std::string &filename,
                                                               uint32_t line_num,
                                                               uint32_t col_num) {
    using namespace sqlite_orm;
    std::vector<BreakPoint> bps;
    auto resolved_filename = resolve_filename_to_db(filename);
    if (use_base_name_) {
        std::filesystem::path p = resolved_filename;
        resolved_filename = p.filename();
    }
    std::lock_guard guard(db_lock_);
    if (col_num != 0) {
        bps = db_->get_all<BreakPoint>(where(c(&BreakPoint::filename) == resolved_filename &&
                                             c(&BreakPoint::line_num) == line_num &&
                                             c(&BreakPoint::column_num) == col_num));
    } else if (line_num != 0) {
        // NOLINTNEXTLINE
        bps = db_->get_all<BreakPoint>(where(c(&BreakPoint::filename) == resolved_filename &&
                                             c(&BreakPoint::line_num) == line_num));
    } else {
        // NOLINTNEXTLINE
        bps = db_->get_all<BreakPoint>(where(c(&BreakPoint::filename) == resolved_filename));
    }

    // need to change the breakpoint filename back to client
    // optimized for locally run
    if (has_src_remap()) [[unlikely]] {
        for (auto &bp : bps) {
            bp.filename = resolve_filename_to_client(bp.filename);
        }
    }
    return bps;
}

std::vector<BreakPoint> DBSymbolTableProvider::get_breakpoints(const std::string &filename) {
    auto resolved_filename = resolve_filename_to_db(filename);
    if (use_base_name_) {  // NOLINT
        std::filesystem::path p = resolved_filename;
        resolved_filename = p.filename();
    }
    // NOLINTNEXTLINE
    return get_breakpoints(resolved_filename, 0, 0);
}

std::optional<BreakPoint> DBSymbolTableProvider::get_breakpoint(uint32_t breakpoint_id) {
    std::lock_guard guard(db_lock_);
    auto ptr = db_->get_pointer<BreakPoint>(breakpoint_id);  // NOLINT
    if (ptr) {
        if (has_src_remap()) [[unlikely]] {
            ptr->filename = resolve_filename_to_client(ptr->filename);
        }
        // notice that BreakPoint has a unique_ptr, so we can't just return the de-referenced value
        return BreakPoint{.id = ptr->id,
                          .instance_id = std::make_unique<uint32_t>(*ptr->instance_id),
                          .filename = ptr->filename,
                          .line_num = ptr->line_num,
                          .column_num = ptr->column_num,
                          .condition = ptr->condition};

    } else {
        return std::nullopt;
    }
}

std::optional<std::string> DBSymbolTableProvider::get_instance_name(uint32_t id) {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    // NOLINTNEXTLINE
    auto value = db_->get_pointer<Instance>(id);
    if (value) {
        return value->name;
    } else {
        return {};
    }
}

std::optional<uint64_t> DBSymbolTableProvider::get_instance_id(const std::string &instance_name) {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    // although instance_name is not indexed, it will be only used when the simulator
    // is paused, hence performance is not the primary concern
    auto value = db_->select(columns(&Instance::id), where(c(&Instance::name) == instance_name));
    if (!value.empty()) {
        return std::get<0>(value[0]);
    } else {
        return {};
    }
}

std::optional<uint64_t> DBSymbolTableProvider::get_instance_id(uint64_t breakpoint_id) {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    auto value =
        db_->select(columns(&BreakPoint::instance_id), where(c(&BreakPoint::id) == breakpoint_id));
    if (!value.empty()) {
        return *std::get<0>(value[0]);
    } else {
        return std::nullopt;
    }
}

std::string get_var_value(bool is_rtl, const std::string &value, const std::string &instance_name) {
    std::string fullname;
    if (is_rtl && value.find(instance_name) == std::string::npos) {
        fullname = fmt::format("{0}.{1}", instance_name, value);
    } else {
        fullname = value;
    }
    return fullname;
}

std::vector<DBSymbolTableProvider::ContextVariableInfo>
DBSymbolTableProvider::get_context_variables(uint32_t breakpoint_id) {
    using namespace sqlite_orm;
    std::vector<DBSymbolTableProvider::ContextVariableInfo> result;
    std::lock_guard guard(db_lock_);
    // NOLINTNEXTLINE
    auto values = db_->select(
        columns(&ContextVariable::variable_id, &ContextVariable::name, &Variable::value,
                &Variable::is_rtl, &Instance::name),
        where(c(&ContextVariable::breakpoint_id) == breakpoint_id &&
              c(&ContextVariable::variable_id) == &Variable::id &&
              c(&Instance::id) == &BreakPoint::instance_id && c(&BreakPoint::id) == breakpoint_id));
    result.reserve(values.size());
    for (auto const &[variable_id, name, value, is_rtl, instance_name] : values) {
        auto id = *variable_id;
        auto actual_value = get_var_value(is_rtl, value, instance_name);
        result.emplace_back(std::make_pair(
            ContextVariable{.name = name,
                            .breakpoint_id = std::make_unique<uint32_t>(breakpoint_id),
                            .variable_id = std::make_unique<uint32_t>(id)},
            Variable{.id = id, .value = actual_value, .is_rtl = is_rtl}));
    }
    return result;
}

std::vector<DBSymbolTableProvider::GeneratorVariableInfo>
DBSymbolTableProvider::get_generator_variable(uint32_t instance_id) {
    using namespace sqlite_orm;
    std::vector<DBSymbolTableProvider::GeneratorVariableInfo> result;
    std::lock_guard guard(db_lock_);
    // NOLINTNEXTLINE
    auto values = db_->select(columns(&GeneratorVariable::variable_id, &GeneratorVariable::name,
                                      &Variable::value, &Variable::is_rtl, &Instance::name),
                              where(c(&GeneratorVariable::instance_id) == instance_id &&
                                    c(&GeneratorVariable::variable_id) == &Variable::id &&
                                    c(&Instance::id) == instance_id));
    result.reserve(values.size());
    for (auto const &[variable_id, name, value, is_rtl, instance_name] : values) {
        auto id = *variable_id;
        auto actual_value = get_var_value(is_rtl, value, instance_name);
        result.emplace_back(
            std::make_pair(GeneratorVariable{.name = name,
                                             .instance_id = std::make_unique<uint32_t>(instance_id),
                                             .variable_id = std::make_unique<uint32_t>(id)},
                           Variable{.id = id, .value = actual_value, .is_rtl = is_rtl}));
    }
    return result;
}

std::vector<std::string> DBSymbolTableProvider::get_instance_names() {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    auto instances = db_->get_all<Instance>();  // NOLINT
    std::vector<std::string> result;
    result.reserve(instances.size());
    for (auto const &inst : instances) {
        result.emplace_back(inst.name);
    }
    return result;
}

std::vector<std::string> DBSymbolTableProvider::get_filenames() {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    auto names = db_->select(distinct(&BreakPoint::filename));  // NOLINT
    return names;
}

std::vector<std::string> DBSymbolTableProvider::get_annotation_values(const std::string &name) {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    auto values = db_->select(columns(&Annotation::value), where(c(&Annotation::name) == name));
    std::vector<std::string> result;
    result.reserve(values.size());
    for (auto const &[v] : values) {
        result.emplace_back(v);
    }
    return result;
}

std::vector<std::string> DBSymbolTableProvider::get_all_array_names() {
    using namespace sqlite_orm;
    if (!db_) return {};
    std::set<std::string> names;
    auto result = db_->select(
        columns(&Variable::value, &Instance::name),
        where(c(&Instance::id) == &GeneratorVariable::instance_id &&
              c(&GeneratorVariable::variable_id) == &Variable::id && c(&Variable::is_rtl) == true));
    for (auto const &[name, instance_name] : result) {
        auto v = get_var_value(true, name, instance_name);
        names.emplace(v);
    }

    result = db_->select(
        columns(&Variable::value, &Instance::name),
        where(c(&Instance::id) == &BreakPoint::instance_id &&
              c(&ContextVariable::breakpoint_id) == &BreakPoint::id &&
              c(&ContextVariable::variable_id) == &Variable::id && c(&Variable::is_rtl) == true));
    for (auto const &[name, instance_name] : result) {
        auto v = get_var_value(true, name, instance_name);
        names.emplace(v);
    }

    auto r = std::vector<std::string>(names.begin(), names.end());
    return r;
}

std::vector<std::tuple<uint32_t, std::string, std::string>>
DBSymbolTableProvider::get_assigned_breakpoints(const std::string &var_name,  // NOLINT
                                                uint32_t breakpoint_id) {
    using namespace sqlite_orm;
    if (!db_) return {};
    // need to get reference breakpoint
    auto ref_bp = get_breakpoint(breakpoint_id);
    if (!ref_bp) return {};
    auto ref_assignments =
        db_->get_all<AssignmentInfo>(where(c(&AssignmentInfo::breakpoint_id) == breakpoint_id));
    auto inst = get_instance_name_from_bp(breakpoint_id);
    if (ref_assignments.empty() || !inst) return {};
    std::string target_var_name = var_name;
    bool found = false;
    bool member_access = false;
    AssignmentInfo ref_assign;
    if (ref_assignments.size() > 1) {
        // this is an array assignment
        // trying to find exact match through some heuristics
        for (auto &ref_assignment : ref_assignments) {
            if (ref_assignment.name == target_var_name) {
                found = true;
                ref_assign = std::move(ref_assignment);
                break;
            }
        }
    } else {
        auto &ref_assignment = ref_assignments[0];
        // need to clean the variable name
        if ((var_name.find('[') != std::string::npos || var_name.find('.') != std::string::npos) &&
            // maybe the generator already taken care of it
            ref_assignment.name != var_name) {
            auto tokens = util::get_tokens(var_name, "[.");
            target_var_name = {tokens[0]};
            found = true;
            member_access = true;
            ref_assign = std::move(ref_assignment);
        } else if (ref_assignment.name == var_name) {
            found = true;
        }
    }

    if (!found) return {};
    std::vector<std::tuple<uint32_t, std::string, std::string>> result;
    std::vector<std::tuple<std::unique_ptr<uint32_t>, std::string, std::string>> res;
    if (ref_assign.scope_id) {
        res = db_->select(columns(&AssignmentInfo::breakpoint_id, &AssignmentInfo::value,
                                  &AssignmentInfo::condition),
                          where(c(&AssignmentInfo::scope_id) == *ref_assign.scope_id &&
                                c(&AssignmentInfo::name) == target_var_name &&
                                c(&BreakPoint::id) == (&AssignmentInfo::breakpoint_id) &&
                                c(&BreakPoint::instance_id) == *ref_bp->instance_id));

    } else {
        // no scope, search all variable information
        res = db_->select(columns(&AssignmentInfo::breakpoint_id, &AssignmentInfo::value,
                                  &AssignmentInfo::condition),
                          where(c(&AssignmentInfo::name) == target_var_name &&
                                c(&BreakPoint::id) == (&AssignmentInfo::breakpoint_id) &&
                                c(&BreakPoint::instance_id) == *ref_bp->instance_id));
    }
    for (auto const &r : res) {
        // need to recover the actual RTL name if it's a member access
        auto name = std::get<1>(r);
        if (member_access) {
            auto tokens = util::get_tokens(var_name, "[.]");
            for (auto i = 1u; i < tokens.size(); i++) {
                auto const &select = tokens[i];
                if (std::all_of(select.begin(), select.end(), ::isdigit)) {
                    name = fmt::format("{0}[{1}]", name, select);
                } else {
                    name = fmt::format("{0}.{1}", name, select);
                }
            }
        }

        result.emplace_back(std::make_tuple(*std::get<0>(r), name, std::get<2>(r)));
    }

    return result;
}

DBSymbolTableProvider::~DBSymbolTableProvider() { close(); }

std::vector<uint32_t> DBSymbolTableProvider::execution_bp_orders() {
    // NOLINTNEXTLINE
    auto scopes = db_->get_all<Scope>();
    if (scopes.empty()) {
        return build_execution_order_from_bp();
    }
    std::vector<uint32_t> result;
    for (auto const &scope : scopes) {
        auto const &ids = scope.breakpoints;
        auto id_tokens = util::get_tokens(ids, " ");
        for (auto const &s_id : id_tokens) {
            auto id = std::stoul(s_id);
            result.emplace_back(id);
        }
    }
    return result;
}

std::vector<uint32_t> DBSymbolTableProvider::build_execution_order_from_bp() {
    // use map's ordered ability
    std::map<std::string, std::map<uint32_t, std::vector<uint32_t>>> bp_ids;
    std::lock_guard guard(db_lock_);
    auto bps = db_->get_all<BreakPoint>();
    for (auto const &bp : bps) {
        bp_ids[bp.filename][bp.line_num].emplace_back(bp.id);
    }
    std::vector<uint32_t> result;
    for (auto &iter : bp_ids) {
        result.reserve(result.size() + iter.second.size());
        for (auto const &iter_ : iter.second) {
            for (auto const bp : iter_.second) result.emplace_back(bp);
        }
    }
    return result;
}

void DBSymbolTableProvider::compute_use_base_name() {
    // if there is any filename that's not absolute path
    // we have to report that
    using namespace sqlite_orm;
    auto filenames = db_->select(&BreakPoint::filename);
    std::unordered_set<std::string> filename_set;
    for (auto const &filename : filenames) filename_set.emplace(filename);
    for (auto const &filename : filename_set) {
        std::filesystem::path p = filename;
        if (!p.is_absolute()) {
            use_base_name_ = true;
            break;
        }
    }
}

namespace db::json {
enum class ScopeEntryType { None, Declaration, Block, Assign, Module };

using ModuleDefDict = std::unordered_map<std::string, std::shared_ptr<db::json::ModuleDef>>;

struct ScopeEntry {
    uint32_t line = 0;
    uint32_t column = 0;
    std::string condition;

    const ScopeEntry *parent = nullptr;

    ScopeEntryType type = ScopeEntryType::None;

    explicit ScopeEntry(ScopeEntryType type) : type(type) {}

    [[nodiscard]] const ScopeEntry *get_previous() const;
    [[nodiscard]] virtual const std::vector<std::shared_ptr<ScopeEntry>> *get_scope() const {
        return nullptr;
    }

    virtual ~ScopeEntry() = default;

    [[nodiscard]] std::string get_condition() const {
        std::string cond = condition;
        auto const *p = parent;
        while (p) {
            if (!p->condition.empty()) {
                if (cond.empty()) {
                    cond = p->condition;
                } else {
                    cond.append(" && ").append(p->condition);
                }
            }

            p = p->parent;
        }
        return cond;
    }

    [[nodiscard]] const std::string &get_filename() const;
};

using VariableType = DBSymbolTableProvider::VariableType;

struct VarDef {
    std::string name;
    std::string value;
    bool rtl = true;
    VariableType type = VariableType::normal;
};

struct Instance {
    const ModuleDef *definition = nullptr;
    std::string name;
    uint32_t id = 0;
    const Instance *parent = nullptr;

    std::unordered_map<std::string, std::unique_ptr<Instance>> instances;
    std::map<uint32_t, const ScopeEntry *> bps;

    [[nodiscard]] inline std::optional<uint32_t> get_bp_id(const ScopeEntry *entry) const {
        // maybe need to do reversed indexing, but good enough for now
        for (auto const &[id_, e] : bps) {
            if (e == entry) return id_;
        }
        return std::nullopt;
    }
};

struct GenericEntry : public ScopeEntry {
    GenericEntry() : ScopeEntry(ScopeEntryType::None) {}
};

struct BlockEntry;
struct ModuleDef : public ScopeEntry {
    std::string name;

    std::vector<std::shared_ptr<ScopeEntry>> scope;

    std::vector<std::shared_ptr<VarDef>> vars;
    std::map<std::string, const ModuleDef *> instances;
    // unresolved instances
    std::map<std::string, std::string> unresolved_instances;
    // fast locate filenames
    std::unordered_set<const BlockEntry *> filename_blocks;

    explicit ModuleDef() : ScopeEntry(ScopeEntryType::Module) {}

    [[nodiscard]] const std::vector<std::shared_ptr<ScopeEntry>> *get_scope() const override {
        return &scope;
    }
};

struct VarDeclEntry : public ScopeEntry {
    std::vector<std::shared_ptr<VarDef>> vars;
    VarDeclEntry() : ScopeEntry(ScopeEntryType::Declaration) {}
};

struct AssignEntry : public ScopeEntry {
    std::vector<std::shared_ptr<VarDef>> vars;

    struct IndexInfo {
        std::shared_ptr<VarDef> var;
        uint32_t min = 0;
        uint32_t max = 0;
    };

    IndexInfo index;

    AssignEntry() : ScopeEntry(ScopeEntryType::Assign) {}

    [[nodiscard]] bool has_index() const { return index.var != nullptr; }
};

struct BlockEntry : public ScopeEntry {
    std::vector<std::shared_ptr<ScopeEntry>> scope;
    std::string filename;

    BlockEntry() : ScopeEntry(ScopeEntryType::Block) {}

    [[nodiscard]] const std::vector<std::shared_ptr<ScopeEntry>> *get_scope() const override {
        return &scope;
    }
};

const ScopeEntry *ScopeEntry::get_previous() const {
    // need to go to parent scope to figure the previous sibling
    if (!parent) return nullptr;
    auto const *scope = parent->get_scope();
    if (!scope) return nullptr;
    for (auto i = 1u; i < scope->size(); i++) {
        if ((*scope)[i].get() == this) {
            return (*scope)[i - 1].get();
        }
    }
    return nullptr;
}

const std::string &ScopeEntry::get_filename() const {
    auto const *block = this;
    while (block) {
        if (block->type == ScopeEntryType::Block) {
            auto const *b = reinterpret_cast<const BlockEntry *>(block);
            if (!b->filename.empty()) return b->filename;
        }
        block = block->parent;
    }
    static std::string empty = {};
    return empty;
}

struct JSONParseInfo {
    const ScopeEntry *current_scope = nullptr;

    std::unordered_map<std::string, std::shared_ptr<ModuleDef>> &module_defs;
    std::unordered_map<std::string, std::shared_ptr<VarDef>> &var_defs;
    std::vector<std::pair<std::string, std::string>> &attributes;

    std::string error_reason;

    JSONParseInfo(std::unordered_map<std::string, std::shared_ptr<ModuleDef>> &module_defs,
                  std::unordered_map<std::string, std::shared_ptr<VarDef>> &var_defs,
                  std::vector<std::pair<std::string, std::string>> &attributes)
        : module_defs(module_defs), var_defs(var_defs), attributes(attributes) {}
};

std::shared_ptr<ModuleDef> parse_module_def(const rapidjson::Value &value, JSONParseInfo &info);
std::shared_ptr<BlockEntry> parse_block_entry(const rapidjson::Value &value, JSONParseInfo &info);
std::shared_ptr<VarDeclEntry> parse_var_decl(const rapidjson::Value &value, JSONParseInfo &info);
std::shared_ptr<AssignEntry> parse_assign(const rapidjson::Value &value, JSONParseInfo &info);
std::vector<std::shared_ptr<VarDef>> parse_var(const rapidjson::Value &value, JSONParseInfo &info);

void set_scope_entry_value(const rapidjson::Value &value, ScopeEntry &result) {
    if (value.HasMember("line")) {
        result.line = value["line"].GetUint();
    }
    if (value.HasMember("column")) {
        result.column = value["column"].GetUint();
    }
    if (value.HasMember("condition")) {
        result.condition = value["condition"].GetString();
    }
}

std::shared_ptr<ScopeEntry> parse_scope_entry(const rapidjson::Value &value, JSONParseInfo &info) {
    std::string_view type = value["type"].GetString();
    std::shared_ptr<ScopeEntry> result;
    if (type == "module") {
        result = parse_module_def(value, info);
    } else if (type == "block") {
        result = parse_block_entry(value, info);
    } else if (type == "decl") {
        result = parse_var_decl(value, info);
    } else if (type == "assign") {
        result = parse_assign(value, info);
    } else if (type == "none") {
        result = std::make_shared<GenericEntry>();
        set_scope_entry_value(value, *result);
    }
    if (result) {
        result->parent = info.current_scope;
    }
    return result;
}

std::shared_ptr<ModuleDef> parse_module_def(const rapidjson::Value &value, JSONParseInfo &info) {
    auto result = std::make_shared<ModuleDef>();
    auto const *temp_scope = info.current_scope;
    info.current_scope = result.get();

    result->name = value["name"].GetString();
    info.module_defs.emplace(result->name, result);
    set_scope_entry_value(value, *result);

    // variables
    auto vars = value["variables"].GetArray();
    for (auto const &var : vars) {
        auto v = parse_var(var, info);
        result->vars.insert(result->vars.end(), v.begin(), v.end());
    }

    // scopes
    auto scope = value["scope"].GetArray();
    result->scope.reserve(scope.Size());
    for (auto const &entry : scope) {
        auto ptr = parse_scope_entry(entry, info);
        if (ptr) {
            result->scope.emplace_back(ptr);
        }
    }
    // instances
    if (value.HasMember("instances")) {
        auto instances = value["instances"].GetArray();
        for (auto const &inst : instances) {
            std::string name = inst["name"].GetString();
            std::string mod_name = inst["module"].GetString();
            // put it in the unresolved instances first, since the module declaration might
            // be out of order
            result->unresolved_instances.emplace(name, mod_name);
        }
    }

    // reset the current module
    info.current_scope = temp_scope;
    return result;
}

std::shared_ptr<BlockEntry> parse_block_entry(const rapidjson::Value &value, JSONParseInfo &info) {
    auto result = std::make_unique<BlockEntry>();
    set_scope_entry_value(value, *result);
    if (value.HasMember("filename")) {
        result->filename = value["filename"].GetString();
    }
    auto const *temp_scope = info.current_scope;
    info.current_scope = result.get();

    auto scope = value["scope"].GetArray();
    result->scope.reserve(scope.Size());
    for (auto const &scope_entry : scope) {
        auto e = parse_scope_entry(scope_entry, info);
        result->scope.emplace_back(e);
    }

    info.current_scope = temp_scope;
    return result;
}

std::shared_ptr<VarDeclEntry> parse_var_decl(const rapidjson::Value &value, JSONParseInfo &info) {
    auto result = std::make_shared<VarDeclEntry>();
    set_scope_entry_value(value, *result);

    result->vars = parse_var(value["variable"], info);

    return result;
}
std::shared_ptr<AssignEntry> parse_assign(const rapidjson::Value &value, JSONParseInfo &info) {
    auto result = std::make_shared<AssignEntry>();
    set_scope_entry_value(value, *result);

    result->vars = parse_var(value["variable"], info);

    if (value.HasMember("index")) {
        auto const &index = value["index"];
        result->index.var = parse_var(index["var"], info)[0];
        result->index.min = index["min"].GetUint();
        result->index.max = index["max"].GetUint();
    }

    return result;
}

std::vector<std::shared_ptr<VarDef>> parse_var(const rapidjson::Value &value, JSONParseInfo &info) {
    std::shared_ptr<VarDef> var;
    if (value.IsString()) {
        // get it from the reference
        std::string id = value.GetString();
        auto const &vars = info.var_defs;
        if (vars.find(id) != vars.end()) {
            var = vars.at(id);
        }
    } else {
        var = std::make_shared<VarDef>();
        var->name = value["name"].GetString();
        var->value = value["value"].GetString();
        var->rtl = value["rtl"].GetBool();

        if (value.HasMember("type")) {
            std::string t = value["type"].GetString();
            if (t == "delay") {
                var->type = VariableType::delay;
            }
        }
    }

    if (!var) {
        info.error_reason = "Unable to parse variable definition";
        return {};
    } else {
        return {var};
    }
}

void parse_var_defs(const rapidjson::Document &document, JSONParseInfo &info) {
    if (!document.HasMember("variables")) return;
    auto variables = document["variables"].GetArray();
    for (auto const &var_def : variables) {
        auto var = std::make_shared<VarDef>();
        var->name = var_def["name"].GetString();
        var->value = var_def["value"].GetString();
        var->rtl = var_def["rtl"].GetBool();
        std::string id = var_def["id"].GetString();
        info.var_defs.emplace(id, var);
    }
}

void parse_attributes(const rapidjson::Document &document, JSONParseInfo &info) {
    if (!document.HasMember("attributes")) return;
    auto attributes = document["attributes"].GetArray();
    for (auto const &attr : attributes) {
        std::string name = attr["name"].GetString();
        std::string value = attr["value"].GetString();
        info.attributes.emplace_back(std::make_pair(name, value));
    }
}

std::shared_ptr<Instance> parse(rapidjson::Document &document, JSONParseInfo &info) {
    // parse vars first since we need that to resolve symbol reference during module definition
    // parsing
    parse_var_defs(document, info);

    auto table = document["table"].GetArray();
    auto const *top = document["top"].GetString();
    std::shared_ptr<Instance> result;
    for (auto &entry : table) {
        auto parsed_entry = parse_scope_entry(entry, info);
        // notice that if the entry type is not a module, we are effectively discarding it
        if (parsed_entry->type == ScopeEntryType::Module) {
            auto m = std::reinterpret_pointer_cast<ModuleDef>(parsed_entry);
            if (m->name == top) {
                // making a new instance. notice that the name is the same as module name
                result = std::make_shared<Instance>();
                result->name = m->name;
                result->definition = m.get();
            }
        }
    }
    parse_attributes(document, info);
    return result;
}

template <bool visit_var = false, bool visit_sub_inst = true, bool visit_stmt = false>
class DBVisitor {
public:
    virtual void handle(const Instance &) {}
    virtual void handle(const VarDef &) {}
    virtual void handle(const BlockEntry &) {}
    virtual void handle(const AssignEntry &) {}
    virtual void handle(const ModuleDef &) {}
    virtual void handle(const VarDeclEntry &) {}
    virtual void handle(const GenericEntry &) {}

    virtual void handle_after(const BlockEntry &) {}

    void visit(const ModuleDef &mod) {
        handle(mod);
        if constexpr (visit_var) {
            for (auto const &v : mod.vars) {
                handle(v);
            }
        }

        for (auto const &s : mod.scope) {
            visit(s);
        }

        if constexpr (visit_sub_inst) {
            for (auto const &[n, ptr] : mod.instances) {
                visit(*ptr);
            }
        }
    }

    void visit(const BlockEntry &block) {
        handle(block);
        for (auto const &s : block.scope) {
            visit(s);
        }
        handle_after(block);
    }

    void visit(const std::shared_ptr<ScopeEntry> &entry) {
        switch (entry->type) {
            case ScopeEntryType::Module: {
                visit(*std::reinterpret_pointer_cast<ModuleDef>(entry));
                break;
            }
            case ScopeEntryType::Block: {
                visit(*std::reinterpret_pointer_cast<BlockEntry>(entry));
                break;
            }
            case ScopeEntryType::Assign: {
                handle(*std::reinterpret_pointer_cast<AssignEntry>(entry));
                break;
            }
            case ScopeEntryType::Declaration: {
                handle(*std::reinterpret_pointer_cast<VarDeclEntry>(entry));
                break;
            }
            case ScopeEntryType::None: {
                handle(*std::reinterpret_pointer_cast<GenericEntry>(entry));
            }
            default: {
            }
        }
    }

    virtual void visit(const Instance &inst) {
        handle(inst);
        if constexpr (visit_stmt) {
            auto *def = const_cast<ModuleDef *>(inst.definition);
            visit(*def);
        }
        if constexpr (visit_sub_inst) {
            for (auto const &[_, sub_inst] : inst.instances) {
                visit(*sub_inst);
            }
        }
    }
};

// build instances tree and assign ids
void build_instance_tree(Instance &inst, uint32_t &id) {
    inst.id = id++;

    for (auto const &[name, def] : inst.definition->instances) {
        auto sub = std::make_unique<Instance>();
        sub->name = name;
        sub->id = id++;
        sub->definition = def;
        sub->parent = &inst;
        inst.instances.emplace(name, std::move(sub));
    }

    for (auto const &[_, sub] : inst.instances) {
        build_instance_tree(*sub, id);
    }
}

class BlockReorderingVisitor : public DBVisitor<false, false, false> {
public:
    void handle_after(const BlockEntry &entry) override {
        // because this is post-node visit, we always visit the child first.
        // this implies that by the time this function is called, its children is correct
        auto *block = const_cast<BlockEntry *>(&entry);
        sort_block(block);
        // then the smallest is always the first one
        if (!block->scope.empty()) {
            block->line = block->scope[0]->line;
        }
        // also merge variables
        merge_var(block);
    }

private:
    static void sort_block(BlockEntry *block) {
        // sort by types first
        std::stable_sort(block->scope.begin(), block->scope.end(),
                         [](auto const &a, auto const &b) {
                             return static_cast<uint32_t>(a->type) < static_cast<uint32_t>(b->type);
                         });
        // column first, then line
        std::stable_sort(block->scope.begin(), block->scope.end(),
                         [](auto const &a, auto const &b) { return a->column < b->column; });
        std::stable_sort(block->scope.begin(), block->scope.end(),
                         [](auto const &a, auto const &b) { return a->line < b->line; });
    }

    // this one merge all var decl and assign into the same entry, if they have identical
    // line and column information
    static void merge_var(BlockEntry *block) {
        // notice that at this point we already sorted it by type
        auto &scope = block->scope;
        for (auto i = 0u; i < scope.size(); i++) {
            if (!scope[i]) continue;
            auto &ref = scope[i];
            if ((ref->type == ScopeEntryType::Assign) ||
                (ref->type == ScopeEntryType::Declaration)) {
                for (auto j = i + 1; j < scope.size(); j++) {
                    auto const &target = scope[j];
                    if (target->type != ref->type || target->line != ref->line ||
                        target->column != ref->column || target->condition != ref->condition ||
                        has_index_var(target) || has_index_var(ref)) {
                        break;
                    }
                    // transfer
                    switch (ref->type) {
                        case ScopeEntryType::Assign:
                            transfer_vars<AssignEntry>(ref, target);
                            break;
                        case ScopeEntryType::Declaration:
                            transfer_vars<VarDeclEntry>(ref, target);
                            break;
                        default: {
                            // probably throw exceptions?
                        }
                    }
                    // delete the target
                    scope[j] = nullptr;
                }
            }
        }

        // clean up empty ones
        scope.erase(std::remove_if(scope.begin(), scope.end(), [](auto const &p) { return !p; }),
                    scope.end());
    }

    template <typename T>
    static void transfer_vars(const std::shared_ptr<ScopeEntry> &dst,
                              const std::shared_ptr<ScopeEntry> &src) {
        auto dst_entry = std::reinterpret_pointer_cast<T>(dst);
        auto src_entry = std::reinterpret_pointer_cast<T>(src);
        dst_entry->vars.insert(dst_entry->vars.end(), src_entry->vars.begin(),
                               src_entry->vars.end());
    }

    static bool has_index_var(const std::shared_ptr<ScopeEntry> &dst) {
        if (dst->type == ScopeEntryType::Assign) {
            auto assign = std::reinterpret_pointer_cast<AssignEntry>(dst);
            return assign->has_index();
        }
        return false;
    }
};

void reorder_block_entry(const ModuleDefDict &defs) {
    for (auto const &iter : defs) {
        auto const &def = iter.second;
        BlockReorderingVisitor vis;
        vis.visit(*def);
    }
}

class BlockFilenameVisitor : public DBVisitor<false, false, false> {
public:
    explicit BlockFilenameVisitor(std::unordered_set<const BlockEntry *> &entries)
        : entries_(entries) {}

    void handle(const BlockEntry &entry) override {
        if (!entry.filename.empty()) {
            entries_.emplace(&entry);
        }
    }

private:
    std::unordered_set<const BlockEntry *> &entries_;
};

void collect_filename_blocks(const ModuleDefDict &defs) {
    for (auto const &iter : defs) {
        auto const &def = iter.second;
        BlockFilenameVisitor vis(def->filename_blocks);
        vis.visit(*def);
    }
}

class ScopeEntryVisitor : public DBVisitor<false, false, true> {
public:
    ScopeEntryVisitor(const Instance &inst, uint32_t &id)
        : inst_(const_cast<Instance *>(&inst)), id_(id) {}

    void handle(const AssignEntry &entry) override { handle_(entry); }
    void handle(const VarDeclEntry &entry) override { handle_(entry); }
    void handle(const GenericEntry &entry) override { handle_(entry); }

private:
    Instance *inst_;
    uint32_t &id_;

    void handle_(const ScopeEntry &entry) {
        if (entry.line > 0) {
            inst_->bps.emplace(id_++, &entry);
        }
    }
};

class InstanceBPVisitor : public DBVisitor<false> {
public:
    explicit InstanceBPVisitor(uint32_t &id) : id_(id) {}

    void handle(const Instance &inst) override {
        ScopeEntryVisitor v(inst, id_);
        v.visit(inst);
    }

private:
    uint32_t &id_;
};

void build_bp_ids(Instance &inst, uint32_t &id) {
    // notice that we build BP based on the ordering of the scope. as a result, the ordering
    // of the breakpoints are exactly the same as the ID
    InstanceBPVisitor v(id);
    v.visit(inst);
}

}  // namespace db::json

JSONSymbolTableProvider::JSONSymbolTableProvider(const std::string &filename) {
    {
        auto stream = std::ifstream(filename);
        if (stream.bad()) {
            return;
        }

        auto valid = valid_json(stream);
        if (!valid) {
            log::log(log::log_level::error, "Invalid JSON file " + filename);
            return;
        }
    }

    {
        auto stream = std::ifstream(filename);
        rapidjson::IStreamWrapper isw(stream);
        rapidjson::Document document;
        document.ParseStream(isw);
        db::json::JSONParseInfo info(module_defs_, var_defs_, attributes_);
        root_ = db::json::parse(document, info);
    }
    parse_db();
}

JSONSymbolTableProvider::JSONSymbolTableProvider(std::unique_ptr<JSONSymbolTableProvider> db) {
    root_ = db->root_;
    module_defs_ = db->module_defs_;
    // ownership transfer complete
    db.reset();
}

class BreakPointVisitor : public db::json::DBVisitor<false> {
private:
    enum class SearchType { ByFilename, ByID };

public:
    BreakPointVisitor(std::string filename, uint32_t line_num, uint32_t col_num)
        : filename_(std::move(filename)),
          line_num_(line_num),
          col_num_(col_num),
          type_(SearchType::ByFilename) {}

    explicit BreakPointVisitor(uint32_t id) : id_(id), type_(SearchType::ByID) {}

    void handle(const db::json::Instance &inst) override {
        switch (type_) {
            case SearchType::ByFilename: {
                handle_filename(inst);
                break;
            }
            case SearchType::ByID: {
                handle_id(inst);
                break;
            }
        }
    }

    std::vector<BreakPoint> results;
    std::vector<const db::json::ScopeEntry *> raw_results;

private:
    std::string filename_;
    uint32_t line_num_ = 0;
    uint32_t col_num_ = 0;

    uint32_t id_ = 0;

    SearchType type_;

    static bool inline is_relative(const std::string &filename) {
        auto p = std::filesystem::path(filename);
        return p.is_relative();
    }

    static bool is_filename_equivalent(const std::string &query_path, const std::string &ref_path) {
        // notice that we have to be extra careful here
        // the query filename might mismatch with the filename we have, i.e. relative vs. absolute
        // Chisel only uses basename, but the users requests always send absolute name
        if (ref_path.empty()) return false;
        if (is_relative(ref_path)) {
            auto f = base_name(query_path);
            return ref_path == f;
        } else {
            return query_path == ref_path;
        }
    }

    static std::string base_name(const std::string &filename) {
        auto p = std::filesystem::path(filename);
        return p.filename();
    }

    const db::json::BlockEntry *filename_match(
        const std::unordered_set<const db::json::BlockEntry *> &blocks,
        const db::json::ScopeEntry *entry) {
        auto const *p = entry->parent;
        while (p) {
            if (p->type == db::json::ScopeEntryType::Block) {
                auto const *block = reinterpret_cast<const db::json::BlockEntry *>(p);
                if (blocks.find(block) != blocks.end() &&
                    is_filename_equivalent(filename_, block->filename)) {
                    return block;
                }
            }

            p = p->parent;
        }

        return nullptr;
    }

    void handle_filename(const db::json::Instance &inst) {
        // find targeted scope first
        auto const &scopes = inst.definition->filename_blocks;
        if (scopes.empty()) [[unlikely]] {
            return;
        }
        // loop through the lines
        for (auto const &[bp_id, scope] : inst.bps) {
            if (line_num_ > 0) {
                if (line_num_ != scope->line) continue;
                if (col_num_ > 0) {
                    // need to match with column
                    if (col_num_ != scope->column) continue;
                }
            }
            auto const *blk = filename_match(scopes, scope);
            if (!blk) continue;
            // this is a match
            // need to find out how many instances that has this entry
            BreakPoint bp{.id = bp_id,
                          .instance_id = std::make_unique<uint32_t>(inst.id),
                          .filename = blk->filename,
                          .line_num = scope->line,
                          .column_num = scope->column,
                          .condition = scope->get_condition()};
            results.emplace_back(std::move(bp));
            raw_results.emplace_back(scope);
        }
    }

    void handle_id(const db::json::Instance &inst) {
        auto const &scopes = inst.definition->filename_blocks;
        if (scopes.empty()) [[unlikely]] {
            return;
        }
        // loop through the lines
        for (auto const &[bp_id, scope] : inst.bps) {
            if (bp_id == id_) {
                auto const &filename = scope->get_filename();
                if (filename.empty()) continue;  // very likely a symbol table error;
                // this is a match
                // need to find out how many instances that has this entry
                BreakPoint bp{.id = bp_id,
                              .instance_id = std::make_unique<uint32_t>(inst.id),
                              .filename = filename,
                              .line_num = scope->line,
                              .column_num = scope->column,
                              .condition = scope->get_condition()};
                results.emplace_back(std::move(bp));
                raw_results.emplace_back(scope);
            }
        }
    }
};

std::vector<BreakPoint> JSONSymbolTableProvider::get_breakpoints(const std::string &filename,
                                                                 uint32_t line_num,
                                                                 uint32_t col_num) {
    if (root_) [[likely]] {
        BreakPointVisitor v(filename, line_num, col_num);
        v.visit(*root_);
        return std::move(v.results);
    } else {
        return {};
    }
}

std::vector<BreakPoint> JSONSymbolTableProvider::get_breakpoints(const std::string &filename) {
    if (root_) [[likely]] {
        BreakPointVisitor v(filename, 0, 0);
        v.visit(*root_);
        return std::move(v.results);
    } else {
        return {};
    }
}

std::optional<BreakPoint> JSONSymbolTableProvider::get_breakpoint(uint32_t breakpoint_id) {
    if (root_) [[likely]] {
        BreakPointVisitor v(breakpoint_id);
        v.visit(*root_);
        if (!v.results.empty()) [[likely]] {
            return std::move(v.results[0]);
        }
    }
    return std::nullopt;
}

class InstanceFromInstIDVisitor : public db::json::DBVisitor<false> {
public:
    explicit InstanceFromInstIDVisitor(uint32_t id) : id_(id) {}

    void handle(const db::json::Instance &inst) override {
        if (!result && inst.id == id_) {
            result = &inst;
        }
    }

    const db::json::Instance *result = nullptr;

private:
    uint32_t id_;
};

std::optional<std::string> JSONSymbolTableProvider::get_instance_name(uint32_t id) {
    if (root_) {
        InstanceFromInstIDVisitor v(id);
        v.visit(*root_);
        if (v.result) {
            std::string name;
            auto const *p = v.result;
            while (p) {
                if (name.empty()) {
                    name = p->name;
                } else {
                    name = fmt::format("{0}.{1}", p->name, name);
                }
                p = p->parent;
            }
            return name;
        }
    }
    return std::nullopt;
}

class InstanceByBpIDVisitor : public db::json::DBVisitor<false> {
public:
    explicit InstanceByBpIDVisitor(uint32_t bp_id) : bp_id_(bp_id) {}

    void handle(const db::json::Instance &inst) override {
        if (!result) {
            for (auto const &[id, _] : inst.bps) {
                if (id == bp_id_) {
                    result = &inst;
                    break;
                }
            }
        }
    }

    const db::json::Instance *result = nullptr;

private:
    uint32_t bp_id_;
};

std::optional<uint64_t> JSONSymbolTableProvider::get_instance_id(uint64_t breakpoint_id) {
    if (root_) {
        InstanceByBpIDVisitor v(breakpoint_id);
        v.visit(*root_);
        if (v.result) {
            return v.result->id;
        }
    }

    return std::nullopt;
}

std::optional<uint64_t> JSONSymbolTableProvider::get_instance_id(const std::string &instance_name) {
    if (root_) {
        auto instances = util::get_tokens(instance_name, ".");
        if (instances[0] == root_->name) {
            auto const *p = root_.get();
            for (auto i = 1u; i < instances.size(); i++) {
                auto const &name = instances[i];
                bool found = false;
                for (auto const &[n, def] : p->instances) {
                    if (n == name) {
                        found = true;
                        p = def.get();
                        break;
                    }
                }
                if (!found) return std::nullopt;
            }
            return p->id;
        }
    }

    return std::nullopt;
}

std::vector<SymbolTableProvider::ContextVariableInfo>
JSONSymbolTableProvider::get_context_variables(uint32_t breakpoint_id) {  // NOLINT
    if (!root_) return {};
    BreakPointVisitor v(breakpoint_id);
    v.visit(*root_);
    if (v.raw_results.empty()) return {};
    auto const *entry = v.raw_results[0];
    std::map<std::string, const db::json::VarDef *> vars;

    // walk all the way up to the module and query its previous siblings
    // notice that this is not super efficient, but for now it's fine since it only gets
    // called during user interaction
    while (entry && entry->type != db::json::ScopeEntryType::Module) {
        auto const *node = entry;
        while (auto const *pre = node->get_previous()) {
            if (pre->type == db::json::ScopeEntryType::Declaration) {
                auto const *decl = reinterpret_cast<const db::json::VarDeclEntry *>(pre);
                for (auto const &var : decl->vars) {
                    if (vars.find(var->name) == vars.end()) {
                        vars.emplace(var->name, var.get());
                    }
                }

            } else if (pre->type == db::json::ScopeEntryType::Assign) {
                auto const *assign = reinterpret_cast<const db::json::AssignEntry *>(pre);
                for (auto const &var : assign->vars) {
                    if (vars.find(var->name) == vars.end()) {
                        vars.emplace(var->name, var.get());
                    }
                }
            }
            node = pre;
        }
        entry = entry->parent;
    }

    std::vector<SymbolTableProvider::ContextVariableInfo> result;
    // convert var information
    for (auto const &[name, var] : vars) {
        ContextVariable ctx_var;
        ctx_var.name = name;
        // not used by downstream
        ctx_var.breakpoint_id = nullptr;
        ctx_var.variable_id = nullptr;
        ctx_var.type = static_cast<uint32_t>(var->type);

        Variable db_var;
        db_var.value = var->value;
        db_var.is_rtl = var->rtl;
        db_var.id = 0;
        result.emplace_back(std::make_pair(std::move(ctx_var), db_var));
    }

    // because we walk the stack in reverse order, we need to reverse it to
    // appear that the variable is declared in order
    std::reverse(result.begin(), result.end());

    return result;
}

std::vector<SymbolTableProvider::GeneratorVariableInfo>
JSONSymbolTableProvider::get_generator_variable(uint32_t instance_id) {
    if (!root_) return {};
    InstanceFromInstIDVisitor v(instance_id);
    v.visit(*root_);
    if (!v.result || !v.result->definition) return {};
    auto const *def = v.result->definition;

    auto const &vars = def->vars;

    std::vector<SymbolTableProvider::GeneratorVariableInfo> result;
    for (auto const &var : vars) {
        GeneratorVariable gen_var;
        gen_var.name = var->name;
        gen_var.instance_id = nullptr;
        gen_var.variable_id = nullptr;

        Variable db_var;
        db_var.value = var->value;
        db_var.is_rtl = var->rtl;
        db_var.id = 0;

        result.emplace_back(std::make_pair(std::move(gen_var), db_var));
    }

    return result;
}

class InstanceNameVisitor : public db::json::DBVisitor<false> {
public:
    void handle(const db::json::Instance &inst) override {
        std::string name;
        if (current_instance_names_.empty()) {
            name = inst.name;
        } else {
            name = current_instance_names_.top();
            name = fmt::format("{0}.{1}", name, inst.name);
        }

        names.emplace(name);
        // pushed it to the stack
        current_instance_names_.emplace(name);
    }

    void visit(const db::json::Instance &inst) override {
        db::json::DBVisitor<false>::visit(inst);
        // pop the current names
        current_instance_names_.pop();
    }

    std::set<std::string> names;

private:
    std::stack<std::string> current_instance_names_;
};

std::vector<std::string> JSONSymbolTableProvider::get_instance_names() {
    if (root_) {
        InstanceNameVisitor v;
        v.visit(*root_);
        auto res = std::vector<std::string>(v.names.begin(), v.names.end());
        return res;
    }
    return {};
}

class FilenameVisitor : public db::json::DBVisitor<false, true, false> {
public:
    void handle(const db::json::BlockEntry &block) override {
        if (!block.filename.empty()) names.emplace(block.filename);
    }

    std::set<std::string> names;
};

std::vector<std::string> JSONSymbolTableProvider::get_filenames() {
    if (root_) {
        FilenameVisitor v;
        v.visit(*root_->definition);
        auto res = std::vector<std::string>(v.names.begin(), v.names.end());
        return res;
    }
    return {};
}

std::vector<std::string> JSONSymbolTableProvider::get_annotation_values(const std::string &name) {
    std::vector<std::string> res;
    for (auto const &[attr_name, attr_value] : attributes_) {
        if (attr_name == name) {
            res.emplace_back(attr_value);
        }
    }
    return res;
}

std::vector<std::string> JSONSymbolTableProvider::get_all_array_names() {
    // not supported
    return {};
}

class AssignmentVisitor : public db::json::DBVisitor<false, false, true> {
public:
    struct Info {
        const db::json::AssignEntry *assign;
        std::string rtl_value;
        std::string condition;
    };

    explicit AssignmentVisitor(const std::string &var_name) {
        var_names_ = util::get_tokens(var_name, "[].");
    }

    void handle(const db::json::AssignEntry &assign) override {
        if (auto info = rtl_equivalent(assign)) {
            result.emplace_back(*info);
        }
    }

    std::vector<Info> result;

private:
    std::vector<std::string> var_names_;

    // NOLINTNEXTLINE
    [[nodiscard]] std::optional<Info> rtl_equivalent(const db::json::AssignEntry &assign) const {
        // the problem is the front end name maybe an indexed variable
        if (assign.has_index()) {
            // this is an indexed variable
            // make sure up to the index we are the same
            // notice that once index is present, it's illegal to have multiple vars
            auto var_names = util::get_tokens(assign.vars[0]->name, "[].");
            if (same_var(var_names.begin(), var_names.end(), var_names_.begin(),
                         var_names_.end() - 1)) {
                // need to parse out the actual var_names index
                auto index_str = var_names_.back();
                if (auto idx = util::stoul(index_str)) {
                    // found the actual index. now check if it's within the boundary
                    if (assign.index.min <= idx && assign.index.max >= idx) {
                        // we have found it. create a condition now
                        auto cond = fmt::format("{0} == {1}", assign.index.var->value, *idx);
                        // and rtl value
                        auto value =
                            fmt::format(fmt::format("{0}[{1}]", assign.vars[0]->value, *idx));
                        return Info{&assign, value, cond};
                    }
                }
            }
        }

        for (auto const &var : assign.vars) {
            // notice that we treat [] and . the same
            // so we have to do some processing
            auto var_names = util::get_tokens(var->name, "[].");
            if (same_var(var_names.begin(), var_names.end(), var_names_.begin(),
                         var_names_.end())) {
            }
            if (var_names.size() == var_names_.size()) {
                bool same = true;
                for (auto i = 0u; i < var_names.size(); i++) {
                    if (var_names[i] != var_names_[i]) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    return Info{&assign, var->value, ""};
                }
            }
        }
        return std::nullopt;
    }

    template <typename T, typename K>
    static bool same_var(T lb, T le, K rb, K re) {
        if (std::distance(lb, le) == std::distance(rb, re)) {
            auto itr = rb;
            for (auto itl = lb; itl != le; itl++) {
                if (*itl != *itr) return false;
                itr++;
            }
            return true;
        }
        return false;
    }
};

std::string merge_condition(const std::string &cond1, const std::string &cond2) {
    if (cond1.empty() && cond2.empty()) {
        return {};
    } else if (!cond1.empty() && cond2.empty()) {
        return cond1;
    } else if (cond1.empty() && !cond2.empty()) {
        return cond2;
    } else {
        return fmt::format("{0} && {1}", cond1, cond2);
    }
}

std::vector<std::tuple<uint32_t, std::string, std::string>>
JSONSymbolTableProvider::get_assigned_breakpoints(const std::string &var_name,
                                                  uint32_t breakpoint_id) {
    if (!root_) return {};
    // need to search the entire scope to see where the value is assigned
    // any variable that has the same source name will be used
    // for now we don't support variable shadowing
    BreakPointVisitor v(breakpoint_id);
    v.visit(*root_);
    if (v.raw_results.empty()) return {};
    auto const *scope_entry = v.raw_results[0];
    // need to find the top non-module scope
    auto const *parent = scope_entry;
    while (parent && parent->type != db::json::ScopeEntryType::Module) {
        parent = parent->parent;
    }

    if (!parent || parent->type != db::json::ScopeEntryType::Module) {
        return {};
    }
    auto const &mod_def = *(reinterpret_cast<const db::json::ModuleDef *>(parent));

    InstanceByBpIDVisitor iv(breakpoint_id);
    iv.visit(*root_);
    if (!iv.result) return {};
    auto const *instance = iv.result;

    // the format is id, var_name (rtl), data_condition
    // look through every assignment
    AssignmentVisitor av(var_name);
    av.visit(mod_def);

    std::vector<std::tuple<uint32_t, std::string, std::string>> result;
    for (auto const &info : av.result) {
        auto bp_id = instance->get_bp_id(info.assign);
        if (!bp_id) continue;
        // we need to merge condition as well
        auto cond = merge_condition(info.assign->get_condition(), info.condition);
        result.emplace_back(std::make_tuple(*bp_id, info.rtl_value, cond));
    }

    return result;
}

std::vector<uint32_t> JSONSymbolTableProvider::execution_bp_orders() {
    // because we create the breakpoint ids in order, it's very easy to create the orders
    std::vector<uint32_t> result;
    result.reserve(num_bps_);
    for (auto i = 0u; i < num_bps_; i++) {
        result.emplace_back(i);
    }
    return result;
}

bool JSONSymbolTableProvider::valid_json(std::istream &stream) {
    // we don't use a singleton design because json validation happens rather infrequent, in fact
    // only once per debugging session (loading the symbol table)
    if (stream.bad()) return false;
    rapidjson::Document document;
    rapidjson::IStreamWrapper isw(stream);
    document.ParseStream(isw);
    if (document.HasParseError()) return false;

    // load schema document
    rapidjson::Document schema_document;
    schema_document.Parse(JSON_SCHEMA);
    if (schema_document.HasParseError()) [[unlikely]] {
        // it's unlikely it has errors
        return false;
    }

    valijson::Schema json_schema;
    valijson::SchemaParser schema_parser;
    valijson::adapters::RapidJsonAdapter schema_adapter(schema_document);
    schema_parser.populateSchema(schema_adapter, json_schema);

    valijson::Validator validator;
    valijson::adapters::RapidJsonAdapter document_adapter(document);
    return validator.validate(json_schema, document_adapter, nullptr);
}

bool JSONSymbolTableProvider::parse(const std::string &db_content) {
    {
        std::stringstream ss;
        ss << db_content;

        auto valid = valid_json(ss);
        if (!valid) {
            return false;
        }
    }

    {
        std::stringstream ss;
        ss << db_content;
        rapidjson::IStreamWrapper isw(ss);
        rapidjson::Document document;
        document.ParseStream(isw);
        db::json::JSONParseInfo info(module_defs_, var_defs_, attributes_);
        root_ = db::json::parse(document, info);

        parse_db();

        if (!info.error_reason.empty()) {
            // we have an error
            log::log(log::log_level::error, info.error_reason);
            root_ = nullptr;
        }
    }
    return root_ != nullptr;
}

void resolve_module_instances(db::json::ModuleDef &def, db::json::ModuleDefDict &defs,
                              bool &has_error) {
    if (has_error) return;
    std::set<db::json::ModuleDef *> subs;
    for (auto const &[name, module] : def.unresolved_instances) {
        if (defs.find(module) == defs.end()) {
            has_error = true;
            return;
        } else {
            auto *ptr = defs.at(module).get();
            def.instances.emplace(name, ptr);
            subs.emplace(ptr);
        }
    }
    def.unresolved_instances.clear();
    // recursively check the child instances
    for (auto *m : subs) {
        resolve_module_instances(*m, defs, has_error);
    }
}

void JSONSymbolTableProvider::parse_db() {
    if (root_ && root_->definition) {
        // resolve the module names
        bool has_error = false;
        resolve_module_instances(*(const_cast<db::json::ModuleDef *>(root_->definition)),
                                 module_defs_, has_error);
        if (has_error) {
            log::log(log::log_level::error, "Unable to resolve all referenced instances");
            root_ = nullptr;
            return;
        }
        // build up the instance tree
        uint32_t inst_id = 0;
        db::json::build_instance_tree(*root_, inst_id);
        // sort the entries. need it done before assigning IDs
        db::json::reorder_block_entry(module_defs_);
        // build the breakpoints table
        build_bp_ids(*root_, num_bps_);
        // index the file names
        db::json::collect_filename_blocks(module_defs_);
    } else {
        root_ = nullptr;
    }
}

}  // namespace hgdb