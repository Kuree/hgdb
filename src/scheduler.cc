#include "scheduler.hh"

#include "debug.hh"
#include "fmt/format.h"
#include "log.hh"
#include "util.hh"

namespace hgdb {

Scheduler::Scheduler(DebuggerNamespaceManager &namespaces, SymbolTableProvider *db,
                     const bool &single_thread_mode, const bool &log_enabled)
    : namespaces_(namespaces),
      db_(db),
      single_thread_mode_(single_thread_mode),
      log_enabled_(log_enabled) {
    // compute the look-up table
    log_info("Compute breakpoint look up table");
    bp_ordering_ = db_->execution_bp_orders();
    for (auto i = 0u; i < bp_ordering_.size(); i++) {
        bp_ordering_table_.emplace(bp_ordering_[i], i);
    }

    // compute clock signals
    // compute the clock signals
    auto clk_names = util::get_clock_signals(namespaces_.default_rtl(), db_);
    clock_handles_.reserve(clk_names.size());
    for (auto const &clk_name : clk_names) {
        auto *handle = namespaces_.default_rtl()->get_handle(clk_name);
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
        if (breakpoints_[i]->evaluated) {
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

    // by default, we generate as many breakpoints as possible to evaluate
    // this can be turned off by client's request (changed via option-change request)
    // notice that we only allow one data breakpoint being triggered for now
    // also notice that since data breakpoint can be mixed with the normal breakpoint, we only
    // disable scanning if it's data breakpoint only
    if (!single_thread_mode_ && result[0]->type != DebugBreakPoint::Type::data) {
        scan_breakpoints(index, true, result);
    }

    // the first will be current breakpoint id since we might skip some of them
    // in the middle
    current_breakpoint_id_ = result.front()->id;
    for (auto *bp : result) {
        bp->evaluated = true;
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
            if (namespaces_.default_rtl()->reverse_last_posedge(handles)) {
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
            auto res = namespaces_.default_rtl()->reverse_last_posedge(handles);
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
            // notice that breakpoints are already ordered,
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
        bp->evaluated = true;
    }
    return result;
}

DebugBreakPoint *Scheduler::get_breakpoint(uint32_t id) const {
    auto pos = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                            [id](auto const &b) { return b->id == id; });
    if (pos != breakpoints_.end()) [[likely]] {
        return pos->get();
    } else {
        return nullptr;
    }
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
    next_temp_breakpoint_.evaluated = true;
    util::validate_expr(namespaces_.default_rtl(), db_, next_temp_breakpoint_.enable_expr.get(),
                        next_temp_breakpoint_.id, next_temp_breakpoint_.instance_id);
    return &next_temp_breakpoint_;
}

std::unique_ptr<DebugBreakPoint> Scheduler::remove_breakpoint(uint64_t bp_id,
                                                              DebugBreakPoint::Type type) {
    // notice that removal doesn't need reordering
    for (auto pos = breakpoints_.begin(); pos != breakpoints_.end(); pos++) {
        if ((*pos)->id == bp_id) {
            // erase the type first. if the remaining type is 0, then we completely erase the
            // breakpoint
            auto t = static_cast<uint32_t>((*pos)->type);
            t &= ~(static_cast<uint32_t>(type));
            if (t == 0) {
                // remove it before after transfer the ownership
                std::unique_ptr<DebugBreakPoint> res = std::move(*pos);
                breakpoints_.erase(pos);
                inserted_breakpoints_.erase(bp_id);
                return res;
            } else {
                (*pos)->type = static_cast<DebugBreakPoint::Type>(t);
            }
            break;
        }
    }
    return nullptr;
}

void clear_breakpoints(std::vector<std::unique_ptr<DebugBreakPoint>> &breakpoints) {
    for (auto &bp : breakpoints) {
        bp->evaluated = false;
    }
}

void Scheduler::start_breakpoint_evaluation() {
    clear_breakpoints(breakpoints_);
    current_breakpoint_id_ = std::nullopt;
}

void Scheduler::set_evaluation_mode(EvaluationMode mode) {
    if (evaluation_mode_ != mode) {
        clear_breakpoints(breakpoints_);
        evaluation_mode_ = mode;
    }
}

void Scheduler::clear() {
    inserted_breakpoints_.clear();
    breakpoints_.clear();
}

// functions that compute the trigger values
std::unordered_map<std::string, vpiHandle> compute_trigger_symbol(const BreakPoint &bp,
                                                                  RTLSimulatorClient *rtl,
                                                                  SymbolTableProvider *db) {
    auto const &trigger_str = bp.trigger;
    auto tokens = util::get_tokens(trigger_str, " ");
    std::unordered_map<std::string, vpiHandle> result;
    if (!tokens.empty()) {
        auto instance_name = db->get_instance_name(*bp.instance_id);
        if (!instance_name) return {};
        for (auto const &symbol : tokens) {
            auto full_name = fmt::format("{0}.{1}", *instance_name, symbol);
            auto *handle = rtl->get_handle(full_name);
            if (!handle) {
                return {};
            }
            result.emplace(symbol, handle);
        }
    }

    return result;
}

// NOLINTNEXTLINE
DebugBreakPoint *Scheduler::add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp,
                                           DebugBreakPoint::Type bp_type, bool data_breakpoint,
                                           const std::string &target_var, bool dry_run) {
    // add them to the eval vector
    std::string cond = "1";
    if (!db_bp.condition.empty()) cond = db_bp.condition;
    if (!bp_info.condition.empty()) {
        cond.append(" && " + bp_info.condition);
    }

    auto insert_bp = [bp_type, this, &db_bp, cond,
                      dry_run](DebuggerNamespace *ns) -> DebugBreakPoint * {
        auto *rtl = ns->rtl.get();
        auto bp = std::make_unique<DebugBreakPoint>();
        bp->id = db_bp.id;
        bp->ns_id = ns->id;
        bp->instance_id = *db_bp.instance_id;
        bp->expr = std::make_unique<DebugExpression>(cond);
        bp->enable_expr =
            std::make_unique<DebugExpression>(db_bp.condition.empty() ? "1" : db_bp.condition);
        bp->filename = db_bp.filename;
        bp->line_num = db_bp.line_num;
        bp->column_num = db_bp.column_num;
        bp->trigger_symbols = compute_trigger_symbol(db_bp, rtl, db_);
        bp->type = bp_type;
        util::validate_expr(rtl, db_, bp->expr.get(), db_bp.id, *db_bp.instance_id);
        if (!bp->expr->correct()) [[unlikely]] {
            log_error("Unable to validate breakpoint expression: " + cond);
            return nullptr;
        }
        util::validate_expr(rtl, db_, bp->enable_expr.get(), db_bp.id, *db_bp.instance_id);
        if (!bp->enable_expr->correct()) [[unlikely]] {
            log_error("Unable to validate breakpoint expression: " + cond);
            return nullptr;
        }

        if (dry_run) {
            // just need a memory holder
            static std::unique_ptr<DebugBreakPoint> holder;
            holder = std::move(bp);
            return holder.get();
        } else {
            auto &ptr = breakpoints_.emplace_back(std::move(bp));
            inserted_breakpoints_.emplace(db_bp.id);
            log_info(
                fmt::format("Breakpoint inserted into {0}:{1}", db_bp.filename, db_bp.line_num));
            return ptr.get();
        }
    };

    std::lock_guard guard(breakpoint_lock_);
    auto instance_name = db_->get_instance_name_from_bp(db_bp.id);
    auto const &namespaces = namespaces_.get_namespaces(instance_name);
    if (!data_breakpoint) [[likely]] {
        if (inserted_breakpoints_.find(db_bp.id) == inserted_breakpoints_.end()) {
            DebugBreakPoint *p = nullptr;
            for (auto *ns : namespaces) {
                p = insert_bp(ns);
            }
            // Only need to return one
            return p;
        } else {
            // update breakpoint entry
            for (auto &b : breakpoints_) {
                if (db_bp.id == b->id) {
                    b->expr = std::make_unique<DebugExpression>(cond);
                    auto *ns = namespaces_[b->ns_id];
                    auto *rtl = ns->rtl.get();
                    util::validate_expr(rtl, db_, b->expr.get(), db_bp.id, *db_bp.instance_id);
                    if (!b->expr->correct()) [[unlikely]] {
                        log_error("Unable to validate breakpoint expression: " + cond);
                    }
                    // need to update the bp type flag
                    b->type = static_cast<DebugBreakPoint::Type>(static_cast<int>(b->type) |
                                                                 static_cast<int>(bp_type));
                    return b.get();
                }
            }
            return nullptr;
        }
    } else {
        // we skip insertion if everything matches
        for (auto const &b : breakpoints_) {
            if (b->id == db_bp.id && b->has_type_flag(DebugBreakPoint::Type::data)) {
                // check if it's data breakpoint as well
                if (b->target_rtl_var_name == target_var && b->expr->expression() == cond) {
                    // no need to insert
                    return b.get();
                }
            }
        }
        DebugBreakPoint *data_bp = nullptr;
        for (auto *ns : namespaces) {
            data_bp = insert_bp(ns);
            auto *rtl = ns->rtl.get();
            auto expr = DebugExpression(target_var);
            util::validate_expr(rtl, db_, &expr, db_bp.id, *db_bp.instance_id);
            auto const &handles = expr.get_resolved_symbol_handles();
            if (!expr.correct() || handles.size() != 1) {
                log_error("Unable to validate variable in data breakpoint: " + target_var);
                return nullptr;
            }
            data_bp->full_rtl_handle = handles.begin()->second;
            data_bp->full_rtl_name = rtl->get_full_name(data_bp->full_rtl_handle);
            data_bp->target_rtl_var_name = target_var;
        }
        return data_bp;
    }
}

DebugBreakPoint *Scheduler::add_data_breakpoint(const std::string &full_name,
                                                const std::string &expression,
                                                const BreakPoint &db_bp, bool dry_run) {
    // we use the same add breakpoint function with different flags on
    BreakPoint bp;
    bp.condition = expression;
    // we allow duplicated breakpoints to be inserted here
    auto *data_bp =
        add_breakpoint(bp, db_bp, DebugBreakPoint::Type::data, true, full_name, dry_run);
    return data_bp;
}

void Scheduler::clear_data_breakpoints() {
    std::lock_guard guard(breakpoint_lock_);
    // if it has both data breakpoints and normal breakpoints, clear the flag
    // else remove this breakpoint
    std::unordered_set<uint32_t> bps;
    for (auto &bp : breakpoints_) {
        if (bp->has_type_flag(DebugBreakPoint::Type::data)) {
            if (bp->has_type_flag(DebugBreakPoint::Type::normal)) {
                bp->type = DebugBreakPoint::Type::normal;
            } else {
                // remove this breakpoint
                bps.emplace(bp->id);
            }
        }
    }
    for (auto id : bps) {
        auto pos = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                                [id](auto const &bp) { return bp->id == id; });
        if (pos != breakpoints_.end()) {
            breakpoints_.erase(pos);
            inserted_breakpoints_.erase(id);
        }
    }
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

void Scheduler::remove_breakpoint(const BreakPoint &bp, DebugBreakPoint::Type type) {
    std::lock_guard guard(breakpoint_lock_);
    remove_breakpoint(bp.id, type);
}

std::optional<uint64_t> Scheduler::remove_data_breakpoint(uint64_t bp_id) {
    std::lock_guard guard(breakpoint_lock_);
    auto ptr = remove_breakpoint(bp_id, DebugBreakPoint::Type::data);
    if (ptr) {
        return ptr->watch_id;
    } else {
        return std::nullopt;
    }
}

std::vector<const DebugBreakPoint *> Scheduler::get_current_breakpoints() {
    std::vector<const DebugBreakPoint *> bps;
    std::lock_guard guard(breakpoint_lock_);
    bps.reserve(breakpoints_.size());
    for (auto const &bp : breakpoints_) {
        bps.emplace_back(bp.get());
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

    // notice that if the reference is a data breakpoint, we're done here, since we only allow
    // one data breakpoint triggerred at a time
    if (ref_bp->has_type_flag(DebugBreakPoint::Type::data)) return;

    // once we have a hit index, scanning down the list to see if we have more
    // hits if it shares the same fn/ln/cn tuple
    // matching criteria:
    // - same enable condition
    // - different instance id

    auto match = [&](uint64_t i) -> bool {
        auto const &next_bp = breakpoints_[i];
        // if fn/ln/cn tuple doesn't match, stop
        // reorder the comparison in a way that exploits short circuit
        if (next_bp->line_num != ref_bp->line_num || next_bp->filename != ref_bp->filename ||
            next_bp->column_num != ref_bp->column_num) {
            return false;
        }
        // if we see a data breakpoint, don't stop, just skip it
        if (next_bp->type == DebugBreakPoint::Type::data) {
            return true;
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
// NOLINTNEXTLINE
void validate_expr(RTLSimulatorClient *rtl, SymbolTableProvider *db, DebugExpression *expr,
                   std::optional<uint32_t> breakpoint_id, std::optional<uint32_t> instance_id) {
    if (!expr->symbols().empty()) {
        // only fill in context static values if the breakpoint has any required symbols
        auto context_static_values = breakpoint_id ? db->get_context_static_values(*breakpoint_id)
                                                   : std::unordered_map<std::string, int64_t>{};
        expr->set_static_values(context_static_values);
    }
    auto required_symbols = expr->get_required_symbols();
    const static std::unordered_set<std::string> predefined_symbols = {time_var_name,
                                                                       instance_var_name};
    for (auto const &symbol : required_symbols) {
        if (predefined_symbols.find(symbol) != predefined_symbols.end()) [[unlikely]] {
            expr->set_resolved_symbol_handle(symbol, nullptr);
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
        }
        // see if it's a valid signal
        bool valid = rtl->is_valid_signal(full_name);
        if (!valid) {
            // try the raw one
            // best effort
            valid = rtl->is_valid_signal(symbol);
            full_name = symbol;
        }
        if (!valid) {
            expr->set_error();
            return;
        }
        auto *handle = rtl->get_handle(full_name);
        expr->set_resolved_symbol_handle(symbol, handle);
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