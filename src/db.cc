#include "db.hh"

#include <filesystem>
#include <unordered_set>

#include "fmt/format.h"
#include "util.hh"

namespace hgdb {

DebugDatabaseClient::DebugDatabaseClient(const std::string &filename) {
    db_ = std::make_unique<DebugDatabase>(init_debug_db(filename));
    db_->sync_schema();

    setup_execution_order();
    compute_use_base_name();
}

DebugDatabaseClient::DebugDatabaseClient(std::unique_ptr<DebugDatabase> db) {
    // this will transfer ownership
    db_ = std::move(db);
    // NOLINTNEXTLINE
    setup_execution_order();
    compute_use_base_name();
}

void DebugDatabaseClient::close() {
    if (!is_closed_) [[likely]] {
        db_.reset();
        is_closed_ = true;
    }
}

std::vector<BreakPoint> DebugDatabaseClient::get_breakpoints(const std::string &filename,
                                                             uint32_t line_num, uint32_t col_num) {
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

std::vector<BreakPoint> DebugDatabaseClient::get_breakpoints(const std::string &filename) {
    auto resolved_filename = resolve_filename_to_db(filename);
    if (use_base_name_) {
        std::filesystem::path p = resolved_filename;
        resolved_filename = p.filename();
    }
    // NOLINTNEXTLINE
    return get_breakpoints(resolved_filename, 0, 0);
}

std::optional<BreakPoint> DebugDatabaseClient::get_breakpoint(uint32_t breakpoint_id) {
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

std::optional<std::string> DebugDatabaseClient::get_instance_name_from_bp(uint32_t breakpoint_id) {
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

std::optional<std::string> DebugDatabaseClient::get_instance_name(uint32_t id) {
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

std::optional<uint64_t> DebugDatabaseClient::get_instance_id(const std::string &instance_name) {
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

std::string get_var_value(bool is_rtl, const std::string &value, const std::string &instance_name) {
    std::string fullname;
    if (is_rtl && value.find(instance_name) == std::string::npos) {
        fullname = fmt::format("{0}.{1}", instance_name, value);
    } else {
        fullname = value;
    }
    return fullname;
}

std::vector<DebugDatabaseClient::ContextVariableInfo> DebugDatabaseClient::get_context_variables(
    uint32_t breakpoint_id, bool resolve_hierarchy_value) {
    using namespace sqlite_orm;
    std::vector<DebugDatabaseClient::ContextVariableInfo> result;
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
        auto actual_value =
            resolve_hierarchy_value ? get_var_value(is_rtl, value, instance_name) : value;
        result.emplace_back(std::make_pair(
            ContextVariable{.name = name,
                            .breakpoint_id = std::make_unique<uint32_t>(breakpoint_id),
                            .variable_id = std::make_unique<uint32_t>(id)},
            Variable{.id = id, .value = actual_value, .is_rtl = is_rtl}));
    }
    return result;
}

std::vector<DebugDatabaseClient::GeneratorVariableInfo> DebugDatabaseClient::get_generator_variable(
    uint32_t instance_id, bool resolve_hierarchy_value) {
    using namespace sqlite_orm;
    std::vector<DebugDatabaseClient::GeneratorVariableInfo> result;
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
        auto actual_value =
            resolve_hierarchy_value ? get_var_value(is_rtl, value, instance_name) : value;
        result.emplace_back(
            std::make_pair(GeneratorVariable{.name = name,
                                             .instance_id = std::make_unique<uint32_t>(instance_id),
                                             .variable_id = std::make_unique<uint32_t>(id)},
                           Variable{.id = id, .value = actual_value, .is_rtl = is_rtl}));
    }
    return result;
}

std::vector<std::string> DebugDatabaseClient::get_instance_names() {
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

std::vector<std::string> DebugDatabaseClient::get_annotation_values(const std::string &name) {
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

std::unordered_map<std::string, int64_t> DebugDatabaseClient::get_context_static_values(
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

std::vector<std::string> DebugDatabaseClient::get_all_signal_names() {
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

DebugDatabaseClient::~DebugDatabaseClient() { close(); }

void DebugDatabaseClient::set_src_mapping(const std::map<std::string, std::string> &mapping) {
    src_remap_ = mapping;
}

std::string DebugDatabaseClient::resolve_filename_to_db(const std::string &filename) const {
    namespace fs = std::filesystem;
    // optimize for local use case
    if (src_remap_.empty()) [[likely]]
        return filename;
    for (auto const &[src_path, dst_path] : src_remap_) {
        if (filename.starts_with(src_path)) {
            return resolve(src_path, dst_path, filename);
        }
    }
    return filename;
}

std::string DebugDatabaseClient::resolve_filename_to_client(const std::string &filename) const {
    namespace fs = std::filesystem;
    // optimize for local use case
    if (src_remap_.empty()) [[likely]]
        return filename;
    for (auto const &[dst_path, src_path] : src_remap_) {
        if (filename.starts_with(src_path)) {
            return resolve(src_path, dst_path, filename);
        }
    }
    return filename;
}

std::optional<std::string> DebugDatabaseClient::resolve_scoped_name_instance(
    const std::string &scoped_name, uint64_t instance_id) {
    auto gen_vars = get_generator_variable(instance_id);
    for (auto const &[gen_var, var] : gen_vars) {
        if (scoped_name == gen_var.name) {
            return var.value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> DebugDatabaseClient::resolve_scoped_name_breakpoint(
    const std::string &scoped_name, uint64_t breakpoint_id) {
    // NOLINTNEXTLINE
    auto context_vars = get_context_variables(breakpoint_id);
    for (auto const &[context_var, var] : context_vars) {
        if (scoped_name == context_var.name) {
            return var.value;
        }
    }
    return std::nullopt;
}

void DebugDatabaseClient::setup_execution_order() {
    auto scopes = db_->get_all<Scope>();
    if (scopes.empty()) {
        build_execution_order_from_bp();
        return;
    }
    for (auto const &scope : scopes) {
        auto const &ids = scope.breakpoints;
        auto id_tokens = util::get_tokens(ids, " ");
        for (auto const &s_id : id_tokens) {
            auto id = std::stoul(s_id);
            execution_bp_orders_.emplace_back(id);
        }
    }
}

void DebugDatabaseClient::build_execution_order_from_bp() {
    // use map's ordered ability
    std::map<std::string, std::map<uint32_t, std::vector<uint32_t>>> bp_ids;
    std::lock_guard guard(db_lock_);
    auto bps = db_->get_all<BreakPoint>();
    for (auto const &bp : bps) {
        bp_ids[bp.filename][bp.line_num].emplace_back(bp.id);
    }
    for (auto &iter : bp_ids) {
        execution_bp_orders_.reserve(execution_bp_orders_.size() + iter.second.size());
        for (auto const &iter_ : iter.second) {
            for (auto const bp : iter_.second) execution_bp_orders_.emplace_back(bp);
        }
    }
}

std::string DebugDatabaseClient::resolve(const std::string &src_path, const std::string &dst_path,
                                         const std::string &target) {
    namespace fs = std::filesystem;
    if (target.starts_with(src_path)) [[likely]] {
        std::error_code ec;
        auto path = fs::relative(target, src_path, ec);
        if (ec.value()) [[unlikely]]
            return target;
        fs::path start = dst_path;
        auto r = start / path;
        return r;
    } else {
        return target;
    }
}

void DebugDatabaseClient::compute_use_base_name() {
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