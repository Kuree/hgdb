#include "db.hh"

#include <filesystem>
#include <regex>
#include <unordered_set>

#include "fmt/format.h"
#include "util.hh"

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

std::optional<std::string> DBSymbolTableProvider::get_instance_name_from_bp(
    uint32_t breakpoint_id) {
    using namespace sqlite_orm;
    std::lock_guard guard(db_lock_);
    auto value = db_->select(
        columns(&Instance::name),
        where(c(&Instance::id) == &BreakPoint::instance_id && c(&BreakPoint::id) == breakpoint_id));
    if (value.empty())
        return std::nullopt;
    else
        return std::get<0>(value[0]);
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

std::unordered_map<std::string, int64_t> DBSymbolTableProvider::get_context_static_values(
    uint32_t breakpoint_id) {
    // only integer values allowed
    std::unordered_map<std::string, int64_t> result;
    if (!db_) return result;
    auto context_variables = get_context_variables(breakpoint_id);
    for (auto const &bp : context_variables) {
        // non-rtl value only
        if (bp.second.is_rtl) continue;
        auto const &symbol_name = bp.first.name;
        auto const &str_value = bp.second.value;
        try {
            int64_t value = std::stoll(str_value);
            result.emplace(symbol_name, value);
        } catch (const std::invalid_argument &) {
        } catch (const std::out_of_range &) {
        }
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

std::map<uint32_t, std::string> DBSymbolTableProvider::get_assigned_breakpoints(
    const std::string &var_name, uint32_t breakpoint_id) {
    using namespace sqlite_orm;
    if (!db_) return {};
    // need to get reference breakpoint
    auto ref_bp = get_breakpoint(breakpoint_id);
    if (!ref_bp) return {};
    auto ref_assignments =
        db_->get_all<AssignmentInfo>(where(c(&AssignmentInfo::breakpoint_id) == breakpoint_id));
    auto inst = get_instance_name_from_bp(breakpoint_id);
    if (ref_assignments.empty() || !inst) return {};
    // get instance as well
    auto const &ref_assignment = ref_assignments[0];
    if (ref_assignment.name != var_name) return {};
    std::map<uint32_t, std::string> result;
    std::vector<std::tuple<std::unique_ptr<uint32_t>, std::string>> res;
    if (ref_assignment.scope_id) {
        res = db_->select(columns(&AssignmentInfo::breakpoint_id, &AssignmentInfo::value),
                          where(c(&AssignmentInfo::scope_id) == *ref_assignment.scope_id &&
                                c(&AssignmentInfo::name) == var_name &&
                                c(&BreakPoint::id) == (&AssignmentInfo::breakpoint_id) &&
                                c(&BreakPoint::instance_id) == *ref_bp->instance_id));

    } else {
        // no scope, search all variable information
        res = db_->select(columns(&AssignmentInfo::breakpoint_id, &AssignmentInfo::value),
                          where(c(&AssignmentInfo::name) == var_name &&
                                c(&BreakPoint::id) == (&AssignmentInfo::breakpoint_id) &&
                                c(&BreakPoint::instance_id) == *ref_bp->instance_id));
    }
    for (auto const &r : res) {
        auto name = fmt::format("{0}.{1}", *inst, std::get<1>(r));
        result.emplace(*std::get<0>(r), name);
    }

    return result;
}

DBSymbolTableProvider::~DBSymbolTableProvider() { close(); }

std::string convert_dot_notation(const std::string &name) {
    // conversion between two notation
    const static std::regex dot(R"(\.(\d+))");
    const static std::regex bracket(R"(\[(\d+)\])");
    std::smatch match;
    if (std::regex_search(name, match, dot)) {
        // replace it
        auto new_name = std::regex_replace(name, dot, R"([$1])");
        return new_name;
    } else if (std::regex_search(name, match, bracket)) {
        auto new_name = std::regex_replace(name, bracket, R"(.$1)");
        return new_name;
    }
    return name;
}

std::optional<std::string> DBSymbolTableProvider::resolve_scoped_name_instance(
    const std::string &scoped_name, uint64_t instance_id) {
    auto name = convert_dot_notation(scoped_name);
    auto gen_vars = get_generator_variable(instance_id);
    for (auto const &[gen_var, var] : gen_vars) {
        if (name == gen_var.name || scoped_name == gen_var.name) {
            return var.value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> DBSymbolTableProvider::resolve_scoped_name_breakpoint(
    const std::string &scoped_name, uint64_t breakpoint_id) {
    auto name = convert_dot_notation(scoped_name);
    // NOLINTNEXTLINE
    auto context_vars = get_context_variables(breakpoint_id);
    for (auto const &[context_var, var] : context_vars) {
        if (name == context_var.name || scoped_name == context_var.name) {
            return var.value;
        }
    }
    return std::nullopt;
}

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

}  // namespace hgdb