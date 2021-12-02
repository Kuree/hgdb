#include "scheduler.hh"

#include "fmt/format.h"
#include "log.hh"
#include "util.hh"

namespace hgdb {

Scheduler::Scheduler(RTLSimulatorClient *rtl, SymbolTableProvider *db,
                     const bool &single_thread_mode, const bool &log_enabled)
    : rtl_(rtl), db_(db), single_thread_mode_(single_thread_mode), log_enabled_(log_enabled) {
    // compute the look up table
    log_info("Compute breakpoint look up table");
    bp_ordering_ = db_->execution_bp_orders();
    for (auto i = 0u; i < bp_ordering_.size(); i++) {
        bp_ordering_table_.emplace(bp_ordering_[i], i);
    }

    // compute clock signals
    // compute the clock signals
    auto clk_names = util::get_clock_signals(rtl_, db_);
    clock_handles_.reserve(clk_names.size());
    for (auto const &clk_name : clk_names) {
        auto *handle = rtl_->get_handle(clk_name);
        if (handle) clock_handles_.emplace_back(handle);
    }
}

std::vector<DebugBreakPoint *> Scheduler::next_breakpoints() {
    switch (evaluation_mode_) {
        case EvaluationMode::BreakPointOnly: {
            return next_normal_breakpoints();
        }
        case EvaluationMode::StepOver: {
            auto *bp = next_step_over_breakpoint();
            if (bp)
                return {bp};
            else
                return {};
        }
        case EvaluationMode::StepBack: {
            auto *bp = next_step_back_breakpoint();
            if (bp)
                return {bp};
            else
                return {};
        }
        case EvaluationMode::ReverseBreakpointOnly: {
            return next_reverse_breakpoints();
        }
        case EvaluationMode::None: {
            return {};
        }
    }
    return {};
}

DebugBreakPoint *Scheduler::next_step_over_breakpoint() {
    // need to get the actual ordering table
    std::optional<uint32_t> next_breakpoint_id;
    if (!current_breakpoint_id_) [[unlikely]] {
        // need to grab the first one, doesn't matter which one
        if (!bp_ordering_.empty()) next_breakpoint_id = bp_ordering_[0];
    } else {
        auto current_id = *current_breakpoint_id_;
        auto pos = std::find(bp_ordering_.begin(), bp_ordering_.end(), current_id);
        if (pos != bp_ordering_.end()) {
            auto index = static_cast<uint64_t>(std::distance(bp_ordering_.begin(), pos));
            if (index != (bp_ordering_.size() - 1)) {
                next_breakpoint_id = bp_ordering_[index + 1];
            }
        }
    }
    if (!next_breakpoint_id) return nullptr;
    current_breakpoint_id_ = next_breakpoint_id;
    evaluated_ids_.emplace(*current_breakpoint_id_);
    // need to get a new breakpoint
    auto bp_info = db_->get_breakpoint(*current_breakpoint_id_);
    return create_next_breakpoint(bp_info);
}

std::vector<DebugBreakPoint *> Scheduler::next_normal_breakpoints() {
    // if no breakpoint inserted. return early
    std::lock_guard guard(breakpoint_lock_);
    if (breakpoints_.empty()) return {};
    // we need to make the experience the same as debugging software
    // as a result, when user add new breakpoints to the list that has high priority,
    // we need to skip then and evaluate them at the next evaluation cycle
    // maybe revisit this logic later? doesn't seem to be correct to me where there are
    // breakpoint inserted during breakpoint hit
    uint64_t index = 0;
    // find index
    std::optional<uint64_t> pos;
    for (uint64_t i = 0; i < breakpoints_.size(); i++) {
        auto id = breakpoints_[i]->id;
        if (evaluated_ids_.find(id) != evaluated_ids_.end()) {
            pos = i;
        }
    }
    if (pos) {
        // we have a last hit
        if (*pos + 1 < breakpoints_.size()) {
            index = *pos + 1;
        } else {
            // the end
            return {};
        }
    }

    std::vector<DebugBreakPoint *> result{breakpoints_[index].get()};

    // by default we generates as many breakpoints as possible to evaluate
    // this can be turned of by client's request (changed via option-change request)
    if (!single_thread_mode_) {
        scan_breakpoints(index, true, result);
    }

    // the first will be current breakpoint id since we might skip some of them
    // in the middle
    current_breakpoint_id_ = result.front()->id;
    for (auto const *bp : result) {
        evaluated_ids_.emplace(bp->id);
    }
    return result;
}

DebugBreakPoint *Scheduler::next_step_back_breakpoint() {
    std::optional<uint32_t> next_breakpoint_id;
    if (!current_breakpoint_id_) [[unlikely]] {
        // can't roll back if the current breakpoint id is not set
        return nullptr;
    } else {
        auto current_id = *current_breakpoint_id_;
        auto pos = std::find(bp_ordering_.begin(), bp_ordering_.end(), current_id);
        auto index = static_cast<uint64_t>(std::distance(bp_ordering_.begin(), pos));
        if (index != 0) {
            next_breakpoint_id = bp_ordering_[index - 1];
        } else {
            // you get stuck at this breakpoint since we can't technically go back any
            // more
            // unless the simulator supported
            // need to extend RTL client capability to actually reverse timestamp
            // in the future.
            auto const &handles = clock_handles_;
            if (rtl_->reverse_last_posedge(handles)) {
                // we successfully reverse the time
                next_breakpoint_id = bp_ordering_.back();
            } else {
                // fail to reverse time, use the first one
                next_breakpoint_id = bp_ordering_[0];
            }
        }
    }
    if (!next_breakpoint_id) return nullptr;

    current_breakpoint_id_ = next_breakpoint_id;
    evaluated_ids_.emplace(*current_breakpoint_id_);
    // need to get a new breakpoint
    auto bp_info = db_->get_breakpoint(*current_breakpoint_id_);
    return create_next_breakpoint(bp_info);
}

std::vector<DebugBreakPoint *> Scheduler::next_reverse_breakpoints() {
    // if no breakpoint inserted. return early
    std::lock_guard guard(breakpoint_lock_);
    if (breakpoints_.empty()) return {};
    // we basically reverse the search of the normal breakpoint

    std::vector<DebugBreakPoint *> result;

    // if the current breakpoint is the first one already, we need to reverse the time to previous
    // cycle
    std::optional<uint64_t> target_index;
    if (current_breakpoint_id_) {
        if (breakpoints_.front()->id == *current_breakpoint_id_) {
            // we have reached the first one of the current
            // call reverse
            auto handles = clock_handles_;
            auto res = rtl_->reverse_last_posedge(handles);
            if (!res) {
                // can't revert the time. use the current timestamp
                // just return the first one
                target_index = 0;
            } else {
                // time reversed
                current_breakpoint_id_ = std::nullopt;
                target_index = breakpoints_.size() - 1;
            }
        } else {
            // find the next inserted breakpoint
            // notice that breakpoints are already ordered
            // so we search from the beginning
            for (auto i = breakpoints_.size() - 1; i >= 1; i--) {
                auto id = breakpoints_[i]->id;
                if (id == *current_breakpoint_id_) {
                    // find it
                    target_index = i - 1;
                    break;
                }
            }
        }
    } else {
        // just return the last one
        target_index = breakpoints_.size() - 1;
    }

    if (!target_index && target_index >= breakpoints_.size()) {
        current_breakpoint_id_ = std::nullopt;
        return {};
    }
    result.emplace_back(breakpoints_[*target_index].get());
    // if it's not single thread mode
    if (!single_thread_mode_) {
        scan_breakpoints(*target_index, false, result);
    }
    // use the last one as the breakpoint id
    current_breakpoint_id_ = result.back()->id;

    for (auto *bp : result) {
        evaluated_ids_.emplace(bp->id);
    }
    return result;
}

DebugBreakPoint *Scheduler::create_next_breakpoint(const std::optional<BreakPoint> &bp_info) {
    if (!bp_info) return nullptr;
    std::string cond = bp_info->condition.empty() ? "1" : bp_info->condition;
    next_temp_breakpoint_.id = *current_breakpoint_id_;
    next_temp_breakpoint_.instance_id = *bp_info->instance_id;
    next_temp_breakpoint_.enable_expr = std::make_unique<DebugExpression>(cond);
    next_temp_breakpoint_.filename = bp_info->filename;
    next_temp_breakpoint_.line_num = bp_info->line_num;
    next_temp_breakpoint_.column_num = bp_info->column_num;
    util::validate_expr(rtl_, db_, next_temp_breakpoint_.enable_expr.get(),
                        next_temp_breakpoint_.id, next_temp_breakpoint_.instance_id);
    return &next_temp_breakpoint_;
}

void Scheduler::start_breakpoint_evaluation() {
    evaluated_ids_.clear();
    current_breakpoint_id_ = std::nullopt;
}

std::vector<DataBreakPoint *> Scheduler::get_data_breakpoints() const {
    std::vector<DataBreakPoint *> result;
    result.reserve(data_breakpoints_.size());
    for (auto const &iter : data_breakpoints_) {
        result.emplace_back(iter.second.get());
    }

    return result;
}

void Scheduler::set_evaluation_mode(EvaluationMode mode) {
    if (evaluation_mode_ != mode) {
        evaluated_ids_.clear();
        evaluation_mode_ = mode;
    }
}

void Scheduler::clear() {
    inserted_breakpoints_.clear();
    breakpoints_.clear();
    data_breakpoints_.clear();
}

// functions that compute the trigger values
std::vector<std::string> compute_trigger_symbol(const BreakPoint &bp) {
    auto const &trigger_str = bp.trigger;
    auto tokens = util::get_tokens(trigger_str, " ");
    return tokens;
}

void Scheduler::add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp) {
    // add them to the eval vector
    std::string cond = "1";
    if (!db_bp.condition.empty()) cond = db_bp.condition;
    if (!bp_info.condition.empty()) cond.append(" && " + bp_info.condition);
    log_info(fmt::format("Breakpoint inserted into {0}:{1}", db_bp.filename, db_bp.line_num));
    std::lock_guard guard(breakpoint_lock_);
    if (inserted_breakpoints_.find(db_bp.id) == inserted_breakpoints_.end()) {
        auto bp = std::make_unique<DebugBreakPoint>();
        bp->id = db_bp.id;
        bp->instance_id = *db_bp.instance_id;
        bp->expr = std::make_unique<DebugExpression>(cond);
        bp->enable_expr =
            std::make_unique<DebugExpression>(db_bp.condition.empty() ? "1" : db_bp.condition);
        bp->filename = db_bp.filename;
        bp->line_num = db_bp.line_num;
        bp->column_num = db_bp.column_num;
        bp->trigger_symbols = compute_trigger_symbol(db_bp);
        breakpoints_.emplace_back(std::move(bp));
        inserted_breakpoints_.emplace(db_bp.id);
        util::validate_expr(rtl_, db_, breakpoints_.back()->expr.get(), db_bp.id,
                            *db_bp.instance_id);
        if (!breakpoints_.back()->expr->correct()) [[unlikely]] {
            log_error("Unable to validate breakpoint expression: " + cond);
        }
        util::validate_expr(rtl_, db_, breakpoints_.back()->enable_expr.get(), db_bp.id,
                            *db_bp.instance_id);
        if (!breakpoints_.back()->enable_expr->correct()) [[unlikely]] {
            log_error("Unable to validate breakpoint expression: " + cond);
        }
    } else {
        // update breakpoint entry
        for (auto &b : breakpoints_) {
            if (db_bp.id == b->id) {
                b->expr = std::make_unique<DebugExpression>(cond);
                util::validate_expr(rtl_, db_, b->expr.get(), db_bp.id, *db_bp.instance_id);
                if (!b->expr->correct()) [[unlikely]] {
                    log_error("Unable to validate breakpoint expression: " + cond);
                }
                return;
            }
        }
    }
}

bool Scheduler::add_data_breakpoint(const std::string &var_name, const std::string &expression,
                                    const BreakPoint &db_bp) {
    log_info(fmt::format("Data breakpoint {0} inserted into {1}:{2}", expression, db_bp.filename,
                         db_bp.line_num));
    // need to make sure it's a valid expression
    std::string cond = "1";
    if (!expression.empty()) cond = expression;
    if (!db_bp.condition.empty()) cond = cond + " && " + db_bp.condition;
    auto bp = std::make_unique<DataBreakPoint>();
    bp->bp.id = db_bp.id;
    bp->bp.instance_id = *db_bp.instance_id;
    // we don't need to distinguish between normal expr and the actual enable condition since there
    // is no conditional breakpoint
    bp->bp.expr = nullptr;
    bp->bp.enable_expr = std::make_unique<DebugExpression>(cond);
    bp->bp.filename = db_bp.filename;
    bp->bp.line_num = db_bp.line_num;
    bp->bp.column_num = db_bp.column_num;
    bp->bp.trigger_symbols = compute_trigger_symbol(db_bp);

    util::validate_expr(rtl_, db_, bp->bp.enable_expr.get(), db_bp.id, *db_bp.instance_id);

    if (!bp->bp.enable_expr->correct()) {
        log_error("Unable to validate breakpoint expression: " + cond);
        return false;
    }

    // need to validate the var expression as well
    bp->var = std::make_unique<DebugExpression>(var_name);
    util::validate_expr(rtl_, db_, bp->var.get(), db_bp.id, *db_bp.instance_id);
    if (!bp->var->correct()) {
        log_error("Unable to validate data breakpoint variable " + var_name);
        return false;
    }

    std::lock_guard guard(breakpoint_lock_);
    bp->id = data_breakpoints_.size();
    data_breakpoints_.emplace(bp->id, std::move(bp));
    return true;
}

void Scheduler::clear_data_breakpoints() {
    std::lock_guard guard(breakpoint_lock_);
    data_breakpoints_.clear();
}

void Scheduler::reorder_breakpoints() {
    std::lock_guard guard(breakpoint_lock_);
    // need to sort them by the ordering
    // the easiest way is to sort them by their lookup table. assuming the number of
    // breakpoints is relatively small, i.e. < 100, sorting can be efficient and less
    // bug-prone
    std::sort(breakpoints_.begin(), breakpoints_.end(),
              [this](const auto &left, const auto &right) -> bool {
                  return bp_ordering_table_.at(left->id) < bp_ordering_table_.at(right->id);
              });
}

void Scheduler::remove_breakpoint(const BreakPoint &bp) {
    std::lock_guard guard(breakpoint_lock_);
    // notice that removal doesn't need reordering
    for (auto pos = breakpoints_.begin(); pos != breakpoints_.end(); pos++) {
        if ((*pos)->id == bp.id) {
            breakpoints_.erase(pos);
            inserted_breakpoints_.erase(bp.id);
            break;
        }
    }
}

std::vector<BreakPoint> Scheduler::get_current_breakpoints() {
    std::vector<BreakPoint> bps;
    std::lock_guard guard(breakpoint_lock_);
    bps.reserve(breakpoints_.size());
    for (auto const &bp : breakpoints_) {
        auto bp_id = bp->id;
        auto bp_info = db_->get_breakpoint(bp_id);
        if (bp_info) {
            bps.emplace_back(BreakPoint{.id = bp_info->id,
                                        .filename = bp_info->filename,
                                        .line_num = bp_info->line_num,
                                        .column_num = bp_info->column_num});
        }
    }
    return bps;
}

bool Scheduler::breakpoint_only() const {
    return evaluation_mode_ == EvaluationMode::BreakPointOnly ||
           evaluation_mode_ == EvaluationMode::ReverseBreakpointOnly;
}

void Scheduler::log_error(const std::string &msg) { log::log(log::log_level::error, msg); }

void Scheduler::log_info(const std::string &msg) const {
    if (log_enabled_) {
        log::log(log::log_level::info, msg);
    }
}

void Scheduler::scan_breakpoints(uint64_t ref_index, bool forward,
                                 std::vector<DebugBreakPoint *> &result) {
    auto const &ref_bp = breakpoints_[ref_index];
    auto const &target_expr = ref_bp->enable_expr->expression();

    // once we have a hit index, scanning down the list to see if we have more
    // hits if it shares the same fn/ln/cn tuple
    // matching criteria:
    // - same enable condition
    // - different instance id

    auto match = [&](int64_t i) -> bool {
        auto const &next_bp = breakpoints_[i];
        // if fn/ln/cn tuple doesn't match, stop
        // reorder the comparison in a way that exploits short circuit
        if (next_bp->line_num != ref_bp->line_num || next_bp->filename != ref_bp->filename ||
            next_bp->column_num != ref_bp->column_num) {
            return false;
        }
        // same enable expression but different instance id
        if (next_bp->instance_id != ref_bp->instance_id &&
            next_bp->enable_expr->expression() == target_expr) {
            result.emplace_back(next_bp.get());
        }
        return true;
    };

    if (forward) {
        for (auto i = ref_index; i < static_cast<uint64_t>(breakpoints_.size()); i++) {
            if (!match(i)) break;
        }
    } else {
        for (auto i = static_cast<int64_t>(ref_index) - 1; i >= 0; i--) {
            if (!match(i)) break;
        }
    }
}

namespace util {
// gcc 11.2.0 seems to have this bug
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635
// disable this warning for now
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void validate_expr(RTLSimulatorClient *rtl, SymbolTableProvider *db, DebugExpression *expr,
                   std::optional<uint32_t> breakpoint_id, std::optional<uint32_t> instance_id) {
    auto context_static_values = breakpoint_id ? db->get_context_static_values(*breakpoint_id)
                                               : std::unordered_map<std::string, int64_t>{};
    expr->set_static_values(context_static_values);
    auto required_symbols = expr->get_required_symbols();
    const static std::unordered_set<std::string> predefined_symbols = {time_var_name,
                                                                       instance_var_name};
    for (auto const &symbol : required_symbols) {
        if (predefined_symbols.find(symbol) != predefined_symbols.end()) [[unlikely]] {
            expr->set_resolved_symbol_name(symbol, symbol);
            continue;
        }
        std::optional<std::string> name;
        if (breakpoint_id) {
            name = db->resolve_scoped_name_breakpoint(symbol, *breakpoint_id);
            if (!name) {
                // try to elevate to instance-based query
                instance_id = db->get_instance_id(*breakpoint_id);
            }
        }
        if (!name && instance_id) {
            name = db->resolve_scoped_name_instance(symbol, *instance_id);
            // if we still can't get it working. use the VPI instead
            if (!name) {
                auto inst_name = db->get_instance_name(*instance_id);
                if (inst_name) [[likely]]
                    name = fmt::format("{0}.{1}", *inst_name, symbol);
            }
        }
        std::string full_name;
        if (name) [[likely]] {
            full_name = rtl->get_full_name(*name);
        } else {
            // best effort
            full_name = rtl->get_full_name(symbol);
        }
        // see if it's a valid signal
        bool valid = rtl->is_valid_signal(full_name);
        if (!valid) {
            expr->set_error();
            return;
        }
        expr->set_resolved_symbol_name(symbol, full_name);
    }
}
#ifndef __clang__
#pragma GCC diagnostic pop
#endif

std::vector<std::string> get_clock_signals(RTLSimulatorClient *rtl, SymbolTableProvider *db) {
    if (!rtl) return {};
    std::vector<std::string> result;
    if (db) {
        // always load from db first
        auto db_clock_names = db->get_annotation_values("clock");
        for (auto const &name : db_clock_names) {
            auto full_name = rtl->get_full_name(name);
            result.emplace_back(full_name);
        }
    }
    if (result.empty()) {
        // use rtl based heuristics
        result = rtl->get_clocks_from_design();
    }
    return result;
}

}  // namespace util

}  // namespace hgdb