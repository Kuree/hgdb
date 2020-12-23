#include "db.hh"

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
    setup_execution_order();
}

void DebugDatabaseClient::close() {
    if (!is_closed_) {
        db_.reset();
        is_closed_ = true;
    }
}

std::vector<BreakPoint> DebugDatabaseClient::get_breakpoints(const std::string &filename,
                                                             uint32_t line_num, uint32_t col_num) {
    using namespace sqlite_orm;
    std::vector<BreakPoint> bps;
    if (col_num != 0) {
        bps = db_->get_all<BreakPoint>(where(c(&BreakPoint::filename) == filename &&
                                             c(&BreakPoint::line_num) == line_num &&
                                             c(&BreakPoint::column_num) == col_num));
    } else {
        bps = db_->get_all<BreakPoint>(
            where(c(&BreakPoint::filename) == filename && c(&BreakPoint::line_num) == line_num));
    }

    return bps;
}

std::optional<BreakPoint> DebugDatabaseClient::get_breakpoint(uint32_t breakpoint_id) {
    auto ptr = db_->get_pointer<BreakPoint>(breakpoint_id);
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

std::vector<DebugDatabaseClient::ContextVariableInfo> DebugDatabaseClient::get_context_variables(
    uint32_t breakpoint_id) const {
    using namespace sqlite_orm;
    std::vector<DebugDatabaseClient::ContextVariableInfo> result;
    auto values = db_->select(columns(&ContextVariable::variable_id, &ContextVariable::name,
                                      &Variable::value, &Variable::is_rtl),
                              where(c(&ContextVariable::breakpoint_id) == breakpoint_id &&
                                  c(&ContextVariable::variable_id) == &Variable::id));
    result.reserve(values.size());
    for (auto const &[variable_id, name, value, is_rtl] : values) {
        auto id = *variable_id;
        result.emplace_back(std::make_pair(
            ContextVariable{.name = name,
                            .breakpoint_id = std::make_unique<uint32_t>(breakpoint_id),
                            .variable_id = std::make_unique<uint32_t>(id)},
            Variable{.id = id, .value = value, .is_rtl = is_rtl}));
    }
    return result;
}

std::vector<DebugDatabaseClient::GeneratorVariableInfo> DebugDatabaseClient::get_generator_variable(
    uint32_t instance_id) const {
    using namespace sqlite_orm;
    std::vector<DebugDatabaseClient::GeneratorVariableInfo> result;
    auto values = db_->select(columns(&GeneratorVariable::variable_id, &GeneratorVariable::name,
                                      &Variable::value, &Variable::is_rtl),
                              where(c(&GeneratorVariable::instance_id) == instance_id &&
                                  c(&GeneratorVariable::variable_id) == &Variable::id));
    result.reserve(values.size());
    for (auto const &[variable_id, name, value, is_rtl] : values) {
        auto id = *variable_id;
        result.emplace_back(
            std::make_pair(GeneratorVariable{.name = name,
                                             .instance_id = std::make_unique<uint32_t>(instance_id),
                                             .variable_id = std::make_unique<uint32_t>(id)},
                           Variable{.id = id, .value = value, .is_rtl = is_rtl}));
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
    std::map<std::string, std::map<uint32_t, uint32_t>> bp_ids;
    auto bps = db_->get_all<BreakPoint>();
    for (auto const &bp : bps) {
        bp_ids[bp.filename].emplace(bp.line_num, bp.id);
    }
    for (auto const &iter : bp_ids) {
        execution_bp_orders_.reserve(execution_bp_orders_.size() + iter.second.size());
        for (auto const &iter_ : iter.second) {
            execution_bp_orders_.emplace_back(iter_.second);
        }
    }
}

}  // namespace hgdb