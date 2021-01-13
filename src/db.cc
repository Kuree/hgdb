#include "db.hh"

#include "fmt/format.h"
#include "util.hh"

namespace hgdb {

DebugDatabaseClient::DebugDatabaseClient(const std::string &filename) {
    db_ = std::make_unique<DebugDatabase>(init_debug_db(filename));
    db_->sync_schema();

    setup_execution_order();
}

DebugDatabaseClient::DebugDatabaseClient(std::unique_ptr<DebugDatabase> db) {
    // this will transfer ownership
    db_ = std::move(db);
    // NOLINTNEXTLINE
    setup_execution_order();
}

void DebugDatabaseClient::close() {
    if (!is_closed_) [[likely]] {  // NOLINT
        db_.reset();
        is_closed_ = true;
    }
}

std::vector<BreakPoint> DebugDatabaseClient::get_breakpoints(const std::string &filename,
                                                             uint32_t line_num, uint32_t col_num) {
    using namespace sqlite_orm;
    std::vector<BreakPoint> bps;
    std::lock_guard guard(db_lock_);
    if (col_num != 0) {
        bps = db_->get_all<BreakPoint>(where(c(&BreakPoint::filename) == filename &&
                                             c(&BreakPoint::line_num) == line_num &&
                                             c(&BreakPoint::column_num) == col_num));
    } else if (line_num != 0) {
        bps = db_->get_all<BreakPoint>(
            where(c(&BreakPoint::filename) == filename && c(&BreakPoint::line_num) == line_num));
    } else {
        bps = db_->get_all<BreakPoint>(where(c(&BreakPoint::filename) == filename));
    }

    return bps;
}

std::vector<BreakPoint> DebugDatabaseClient::get_breakpoints(const std::string &filename) {
    return get_breakpoints(filename, 0, 0);
}

std::optional<BreakPoint> DebugDatabaseClient::get_breakpoint(uint32_t breakpoint_id) {
    std::lock_guard guard(db_lock_);
    auto ptr = db_->get_pointer<BreakPoint>(breakpoint_id);  // NOLINT
    if (ptr) {
        // notice that BreakPoint has a unique_ptr, so we can't just copy them over
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
    uint32_t breakpoint_id) {
    using namespace sqlite_orm;
    std::vector<DebugDatabaseClient::ContextVariableInfo> result;
    std::lock_guard guard(db_lock_);
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

std::vector<DebugDatabaseClient::GeneratorVariableInfo> DebugDatabaseClient::get_generator_variable(
    uint32_t instance_id) {
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
        auto actual_value = get_var_value(is_rtl, value, instance_name);
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

DebugDatabaseClient::~DebugDatabaseClient() { close(); }

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

}  // namespace hgdb