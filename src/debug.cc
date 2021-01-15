#include "debug.hh"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <thread>

#include "fmt/format.h"
#include "log.hh"
#include "util.hh"

namespace fs = std::filesystem;

namespace hgdb {
Debugger::Debugger() : Debugger(nullptr) {}

Debugger::Debugger(std::unique_ptr<AVPIProvider> vpi) {
    // initialize the RTL client first
    // using the default implementation
    rtl_ = std::make_unique<RTLSimulatorClient>(std::move(vpi));
    // initialize the webserver here
    // need to get information about the port number
    auto port = get_port();
    server_ = std::make_unique<DebugServer>(port);
    log_enabled_ = get_logging();
    log_info(fmt::format("Debugging server started at :{0}", port));
}

bool Debugger::initialize_db(const std::string &filename) {
    // we cannot accept in-memory database since in the debug mode,
    // it is readonly
    if (!fs::exists(filename)) {
        log_error(fmt::format("{0} does not exist", filename));
        return false;
    }
    log_info(fmt::format("Debug database set to {0}", filename));
    initialize_db(std::make_unique<DebugDatabaseClient>(filename));
    return true;
}

void Debugger::initialize_db(std::unique_ptr<DebugDatabaseClient> db) {
    if (!db) return;
    db_ = std::move(db);
    // get all the instance names
    auto instances = db_->get_instance_names();
    log_info("Compute instance mapping");
    rtl_->initialize_instance_mapping(instances);
    // compute the look up table
    log_info("Compute breakpoint look up table");
    auto const &bp_ordering = db_->execution_bp_orders();
    for (auto i = 0u; i < bp_ordering.size(); i++) {
        bp_ordering_table_.emplace(bp_ordering[i], i);
    }
}

void Debugger::run() {
    auto on_ = [this](const std::string &msg) { on_message(msg); };
    server_thread_ = std::thread([on_, this]() {
        server_->set_on_message(on_);
        is_running_ = true;
        server_->run();
    });
    // block this thread until we receive the continue from user
    lock_.wait();
}

void Debugger::stop() {
    // if wait, continue it
    lock_.ready();
    server_->stop();
    is_running_ = false;
}

void Debugger::eval() {
    // the function that actually triggers breakpoints!
    // notice that there is a hidden race condition
    // when we trigger the breakpoint, the runtime (simulation side) will be paused via a lock.
    // however, the server side can still takes breakpoint requests, hence modifying the
    // breakpoints_.
    log_info("Start breakpoint evaluation...");
    start_breakpoint_evaluation();  // clean the state
    while (true) {
        auto bps = next_breakpoints();
        if (bps.empty()) break;
        std::vector<bool> hits(bps.size());
        std::fill(hits.begin(), hits.end(), false);
        // multi-threading for the win
        std::vector<std::thread> threads;
        threads.reserve(bps.size());
        for (auto i = 0u; i < bps.size(); i++) {
            auto *bp = bps[i];
            // lock-free map
            threads.emplace_back(
                std::thread([bp, i, &hits, this]() { this->eval_breakpoint(bp, hits, i); }));
        }
        for (auto &t : threads) t.join();
        std::vector<const DebugBreakPoint *> result;
        result.reserve(bps.size());
        for (auto i = 0u; i < bps.size(); i++) {
            if (hits[i]) result.emplace_back(bps[i]);
        }
        if (!result.empty()) {
            // send the breakpoint hit information
            send_breakpoint_hit(result);
            // then pause the execution
            lock_.wait();
        }
    }
}

[[maybe_unused]] bool Debugger::is_verilator() {
    if (rtl_) {
        return rtl_->is_verilator();
    }
    return false;
}

Debugger::~Debugger() { server_thread_.join(); }

void Debugger::on_message(const std::string &message) {
    // server can only receives request
    auto req = Request::parse_request(message);
    if (req->status() != status_code::success) {
        // send back error message
        auto resp = GenericResponse(status_code::error, *req, req->error_reason());
        send_message(resp.str(log_enabled_));
        return;
    }
    switch (req->type()) {
        case RequestType::connection: {
            // this is a connection request
            auto *r = reinterpret_cast<ConnectionRequest *>(req.get());
            handle_connection(*r);
            break;
        }
        case RequestType::breakpoint: {
            auto *r = reinterpret_cast<BreakPointRequest *>(req.get());
            handle_breakpoint(*r);
            break;
        }
        case RequestType::breakpoint_id: {
            auto *r = reinterpret_cast<BreakPointIDRequest *>(req.get());
            handle_breakpoint_id(*r);
            break;
        }
        case RequestType::bp_location: {
            auto *r = reinterpret_cast<BreakPointLocationRequest *>(req.get());
            handle_bp_location(*r);
            break;
        }
        case RequestType::command: {
            auto *r = reinterpret_cast<CommandRequest *>(req.get());
            handle_command(*r);
            break;
        }
        case RequestType::debugger_info: {
            auto *r = reinterpret_cast<DebuggerInformationRequest *>(req.get());
            handle_debug_info(*r);
            break;
        }
        case RequestType::error: {
            auto *r = reinterpret_cast<ErrorRequest *>(req.get());
            handle_error(*r);
            break;
        }
    }
}

void Debugger::send_message(const std::string &message) {
    if (server_) {
        server_->send(message);
    }
}

uint16_t Debugger::get_port() {
    if (!rtl_) return default_port_num;
    auto args = rtl_->get_argv();
    const static std::string plus_port = "+DEBUG_PORT=";
    for (auto const &arg : args) {
        if (arg.find(plus_port) != std::string::npos) {
            auto port_str = arg.substr(plus_port.size());
            uint16_t value;
            try {
                value = std::stoul(port_str);
            } catch (const std::invalid_argument &) {
                value = default_port_num;
            } catch (const std::out_of_range &) {
                value = default_port_num;
            }
            return value;
        }
    }
    return default_port_num;
}

bool Debugger::get_logging() {
    if (!rtl_) return default_logging;
    auto args = rtl_->get_argv();
    const static std::string plus_arg = "+DEBUG_LOG";
    for (auto const &arg : args) {
        if (arg == plus_arg) return true;
    }
    return default_logging;
}

void Debugger::log_error(const std::string &msg) { log::log(log::log_level::error, msg); }

void Debugger::log_info(const std::string &msg) const {
    if (log_enabled_) {
        log::log(log::log_level::info, msg);
    }
}

std::unordered_map<std::string, int64_t> Debugger::get_context_static_values(
    uint32_t breakpoint_id) {
    // only integer values allowed
    std::unordered_map<std::string, int64_t> result;
    if (!db_) return result;
    auto context_variables = db_->get_context_variables(breakpoint_id);
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

// functions that compute the trigger values
std::vector<std::string> compute_trigger_symbol(const BreakPoint &bp) {
    auto const &trigger_str = bp.trigger;
    auto tokens = util::get_tokens(trigger_str, " ");
    return tokens;
}

void Debugger::add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp) {
    // add them to the eval vector
    std::string cond = "1";
    if (!db_bp.condition.empty()) cond = db_bp.condition;
    if (!bp_info.condition.empty()) cond.append(" and " + bp_info.condition);
    log_info(fmt::format("Breakpoint inserted into {0}:{1}", db_bp.filename, db_bp.line_num));
    std::lock_guard guard(breakpoint_lock_);
    if (inserted_breakpoints_.find(db_bp.id) == inserted_breakpoints_.end()) {
        breakpoints_.emplace_back(DebugBreakPoint{
            .id = db_bp.id,
            .instance_id = *db_bp.instance_id,
            .expr = std::make_unique<DebugExpression>(cond),
            .enable_expr =
                std::make_unique<DebugExpression>(db_bp.condition.empty() ? "1" : db_bp.condition),
            .filename = db_bp.filename,
            .line_num = db_bp.line_num,
            .column_num = db_bp.column_num,
            .trigger_symbols = compute_trigger_symbol(db_bp)});
        inserted_breakpoints_.emplace(db_bp.id);
    } else {
        // update breakpoint entry
        for (auto &b : breakpoints_) {
            if (db_bp.id == b.id) {
                b.expr = std::make_unique<DebugExpression>(cond);
                return;
            }
        }
    }
    // clang-tidy reports memory leak due to the usage of emplace make_unique
    // NOLINTNEXTLINE
}

void Debugger::reorder_breakpoints() {
    std::lock_guard guard(breakpoint_lock_);
    // need to sort them by the ordering
    // the easiest way is to sort them by their lookup table. assuming the number of
    // breakpoints is relatively small, i.e. < 100, sorting can be efficient and less
    // bug-prone
    std::sort(breakpoints_.begin(), breakpoints_.end(),
              [this](const auto &left, const auto &right) -> bool {
                  return bp_ordering_table_.at(left.id) < bp_ordering_table_.at(right.id);
              });
}

void Debugger::remove_breakpoint(const BreakPoint &bp) {
    std::lock_guard guard(breakpoint_lock_);
    // notice that removal doesn't need reordering
    for (auto pos = breakpoints_.begin(); pos != breakpoints_.end(); pos++) {
        if (pos->id == bp.id) {
            breakpoints_.erase(pos);
            inserted_breakpoints_.erase(bp.id);
            break;
        }
    }
}

bool Debugger::has_cli_flag(const std::string &flag) {
    if (!rtl_) return false;
    const auto &argv = rtl_->get_argv();
    return std::any_of(argv.begin(), argv.end(), [&flag](const auto &v) { return v == flag; });
}

std::vector<std::string> Debugger::get_clock_signals() {
    if (!rtl_) return {};
    std::vector<std::string> result;
    if (db_) {
        // always load from db first
        auto db_clock_names = db_->get_annotation_values("clock");
        for (auto const &name : db_clock_names) {
            auto full_name = rtl_->get_full_name(name);
            result.emplace_back(full_name);
        }
    }
    if (result.empty()) {
        // use rtl based heuristics
        result = rtl_->get_clocks_from_design();
    }
    return result;
}

PLI_INT32 eval_hgdb_on_clk(p_cb_data cb_data) {
    // only if the clock value is high
    auto value = cb_data->value->value.integer;
    if (value) {
        auto *raw_debugger = cb_data->user_data;
        auto *debugger = reinterpret_cast<hgdb::Debugger *>(raw_debugger);
        debugger->eval();
    }

    return 0;
}

void Debugger::handle_connection(const ConnectionRequest &req) {
    // if we have a debug cli flag, don't load the db
    bool success = true;
    std::string db_filename = "debug symbol table";
    if (!has_cli_flag(debug_skip_db_load)) {
        db_filename = req.db_filename();
        // path mapping not supported yet
        success = initialize_db(db_filename);
    }

    // if success, need to register call backs on the clocks
    // Verilator is handled differently
    if (success && rtl_ && !rtl_->is_verilator()) {
        // only trigger eval at the posedge clk
        auto clock_signals = get_clock_signals();
        bool r = rtl_->monitor_signals(clock_signals, eval_hgdb_on_clk, this);
        if (!r || clock_signals.empty()) log_error("Failed to register evaluation callback");
    }

    if (success) {
        auto resp = GenericResponse(status_code::success, req);
        send_message(resp.str(log_enabled_));
    } else {
        auto resp = GenericResponse(status_code::error, req,
                                    fmt::format("Unable to find {0}", db_filename));
        send_message(resp.str(log_enabled_));
    }

    log_info("handle_connection finished");
}

void Debugger::handle_breakpoint(const BreakPointRequest &req) {
    if (!check_send_db_error(req.type())) return;

    // depends on whether it is add or remove
    auto const &bp_info = req.breakpoint();
    if (req.bp_action() == BreakPointRequest::action::add) {
        // we need to figure out the ordering and where to insert it
        // the work is done here since inserting/removing breakpoint
        // when the simulation is paused doesn't affect the overall
        // performance. users care less about 0.1s slowdown when dealing
        // with the debugger compared to slowed simulation performance
        auto bps = db_->get_breakpoints(bp_info.filename, bp_info.line_num, bp_info.column_num);
        // need to check if it is empty
        // if so send an error back
        if (bps.empty()) {
            auto error_response = GenericResponse(status_code::error, req,
                                                  fmt::format("{0}:{1} is not a valid breakpoint",
                                                              bp_info.filename, bp_info.line_num));
            send_message(error_response.str(log_enabled_));
            return;
        }

        for (auto const &bp : bps) {
            add_breakpoint(bp_info, bp);
        }

        reorder_breakpoints();
    } else {
        // remove
        auto bps = db_->get_breakpoints(bp_info.filename, bp_info.line_num, bp_info.column_num);

        // notice that removal doesn't need reordering
        for (auto const &bp : bps) {
            remove_breakpoint(bp);
        }
    }
    // tell client we're good
    auto success_resp = GenericResponse(status_code::success, req);
    send_message(success_resp.str(log_enabled_));
}

void Debugger::handle_breakpoint_id(const BreakPointIDRequest &req) {
    if (!check_send_db_error(req.type())) return;
    // depends on whether it is add or remove
    auto const &bp_info = req.breakpoint();
    if (req.bp_action() == BreakPointRequest::action::add) {
        // need to query the db to get the actual breakpoint
        auto bp = db_->get_breakpoint(bp_info.id);
        if (!bp) {
            auto error_response =
                GenericResponse(status_code::error, req,
                                fmt::format("BP ({0}) is not a valid breakpoint", bp_info.id));
            send_message(error_response.str(log_enabled_));
            return;
        }
        add_breakpoint(bp_info, *bp);
    } else {
        remove_breakpoint(bp_info);
    }
    // tell client we're good
    auto success_resp = GenericResponse(status_code::success, req);
    send_message(success_resp.str(log_enabled_));
}

void Debugger::handle_bp_location(const BreakPointLocationRequest &req) {
    // if db is not connected correctly, abort
    if (!check_send_db_error(req.type())) return;

    auto const &filename = req.filename();
    auto const &line_num = req.line_num();
    auto const &column_num = req.column_num();
    std::vector<BreakPoint> bps;
    if (!line_num) {
        bps = db_->get_breakpoints(filename);
    } else {
        auto col_num = column_num ? *column_num : 0;
        bps = db_->get_breakpoints(filename, *line_num, col_num);
    }
    std::vector<BreakPoint *> bps_(bps.size());
    for (auto i = 0u; i < bps.size(); i++) {
        bps_[i] = &bps[i];
    }
    // send breakpoint location response
    auto resp = BreakPointLocationResponse(bps_);
    req.set_token(resp);
    // we don't do pretty print if log is not enabled
    send_message(resp.str(log_enabled_));
}

void Debugger::handle_command(const CommandRequest &req) {
    switch (req.command_type()) {
        case CommandRequest::CommandType::continue_: {
            log_info("handle_command: continue_");
            lock_.ready();
            break;
        }
        case CommandRequest::CommandType::stop: {
            log_info("handle_command: stop");
            lock_.ready();
            rtl_->finish_sim();
            stop();
            break;
        }
        case CommandRequest::CommandType::step_over: {
            log_info("handle_command: step_over");
            // change the mode into step through
            evaluation_mode_ = EvaluationMode::StepOver;
            lock_.ready();
            break;
        }
    }
}

void Debugger::handle_debug_info(const DebuggerInformationRequest &req) {
    switch (req.command_type()) {
        case DebuggerInformationRequest::CommandType::breakpoints: {
            std::vector<BreakPoint> bps;
            std::vector<BreakPoint *> bps_;

            {
                std::lock_guard guard(breakpoint_lock_);
                bps.reserve(breakpoints_.size());
                bps_.reserve(breakpoints_.size());
                for (auto const &bp : breakpoints_) {
                    auto bp_id = bp.id;
                    auto bp_info = db_->get_breakpoint(bp_id);
                    if (bp_info) {
                        bps.emplace_back(BreakPoint{.filename = bp_info->filename,
                                                    .line_num = bp_info->line_num,
                                                    .column_num = bp_info->column_num});
                        bps_.emplace_back(&bps.back());
                    }
                }
            }

            auto resp = DebuggerInformationResponse(bps_);
            req.set_token(resp);
            send_message(resp.str(log_enabled_));
            return;
        }
        default: {
            auto resp = GenericResponse(status_code::error, req, "Unknown debugger info command");
            send_message(resp.str(log_enabled_));
        }
    }
}

void Debugger::handle_error(const ErrorRequest &req) {}

void Debugger::send_breakpoint_hit(const std::vector<const DebugBreakPoint *> &bps) {
    // we send it here to avoid a round trip of client asking for context and send send it
    // back
    auto const *first_bp = bps.front();
    BreakPointResponse resp(rtl_->get_simulation_time(), first_bp->filename, first_bp->line_num,
                            first_bp->column_num);
    for (auto const *bp : bps) {
        // first need to query all the values
        auto bp_id = bp->id;
        auto generator_values = db_->get_generator_variable(bp->instance_id);
        auto context_values = db_->get_context_variables(bp_id);
        auto bp_ptr = db_->get_breakpoint(bp_id);
        auto instance_name = db_->get_instance_name_from_bp(bp_id);
        auto instance_name_str = instance_name ? *instance_name : "";

        BreakPointResponse::Scope scope(bp->instance_id, instance_name_str, bp_id);

        using namespace std::string_literals;
        for (auto const &[gen_var, var] : generator_values) {
            std::string value_str;
            if (var.is_rtl) {
                auto full_name = rtl_->get_full_name(var.value);
                auto value = rtl_->get_value(full_name);
                value_str = value ? std::to_string(*value) : error_value_str;
            } else {
                value_str = var.value;
            }
            auto var_name = gen_var.name;
            scope.add_generator_value(var_name, value_str);
        }

        for (auto const &[gen_var, var] : context_values) {
            std::string value_str;
            if (var.is_rtl) {
                auto full_name = rtl_->get_full_name(var.value);
                auto value = rtl_->get_value(full_name);
                value_str = value ? std::to_string(*value) : error_value_str;
            } else {
                value_str = var.value;
            }
            auto var_name = gen_var.name;
            scope.add_local_value(var_name, value_str);
        }
        resp.add_scope(scope);
    }

    auto str = resp.str(log_enabled_);
    send_message(str);
}

bool Debugger::check_send_db_error(RequestType type) {
    if (!db_) {
        // need to send error response
        auto resp = GenericResponse(status_code::error, type,
                                    "Database is not initialized or is initialized incorrectly");
        send_message(resp.str(log_enabled_));
        return false;
    }
    return true;
}

std::vector<Debugger::DebugBreakPoint *> Debugger::next_breakpoints() {
    if (evaluation_mode_ == EvaluationMode::BreakPointOnly) {
        return next_normal_breakpoints();
    } else if (evaluation_mode_ == EvaluationMode::StepOver) {
        auto *bp = next_step_over_breakpoint();
        if (bp)
            return {bp};
        else
            return {};
    } else {
        return {};
    }
}

Debugger::DebugBreakPoint *Debugger::next_step_over_breakpoint() {
    // need to get the actual ordering table
    auto const &orders = db_->execution_bp_orders();
    std::optional<uint32_t> next_breakpoint_id;
    if (!current_breakpoint_id_) [[unlikely]] {  // NOLINT
        // need to grab the first one, doesn't matter which one
        if (!orders.empty()) next_breakpoint_id = orders[0];
    } else {
        auto current_id = *current_breakpoint_id_;
        auto pos = std::find(orders.begin(), orders.end(), current_id);
        if (pos != orders.end()) {
            auto index = static_cast<uint64_t>(std::distance(orders.begin(), pos));
            if (index != (orders.size() - 1)) {
                next_breakpoint_id = orders[index + 1];
            }
        }
    }
    if (!next_breakpoint_id) return nullptr;
    current_breakpoint_id_ = next_breakpoint_id;
    evaluated_ids_.emplace(*current_breakpoint_id_);
    // need to get a new breakpoint
    auto bp_info = db_->get_breakpoint(*current_breakpoint_id_);
    if (!bp_info) return nullptr;
    std::string cond = bp_info->condition.empty() ? "1" : bp_info->condition;
    step_over_breakpoint_.id = *current_breakpoint_id_;
    step_over_breakpoint_.instance_id = *bp_info->instance_id;
    step_over_breakpoint_.enable_expr = std::make_unique<DebugExpression>(cond);
    step_over_breakpoint_.filename = bp_info->filename;
    step_over_breakpoint_.line_num = bp_info->line_num;
    step_over_breakpoint_.column_num = bp_info->column_num;
    return &step_over_breakpoint_;
}

std::vector<Debugger::DebugBreakPoint *> Debugger::next_normal_breakpoints() {
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
        auto id = breakpoints_[i].id;
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
    // once we have a hit index, scanning down the list to see if we have more
    // hits if it shares the same fn/ln/cn tuple
    // matching criteria:
    // - same enable condition
    // - different
    auto const &ref_bp = breakpoints_[index];
    auto const &target_expr = ref_bp.enable_expr->expression();
    std::vector<Debugger::DebugBreakPoint *> result{&breakpoints_[index]};
    for (uint64_t i = index + 1; i < breakpoints_.size(); i++) {
        auto &next_bp = breakpoints_[i];
        // if fn/ln/cn tuple doesn't match, stop
        // reorder the comparison in a way that exploits short circuit
        if (next_bp.line_num != ref_bp.line_num || next_bp.filename != ref_bp.filename ||
            next_bp.column_num != ref_bp.column_num) {
            break;
        }
        // same enable expression but different instance id
        if (next_bp.instance_id != ref_bp.instance_id &&
            next_bp.enable_expr->expression() == target_expr) {
            result.emplace_back(&next_bp);
        }
    }
    // the first will be current breakpoint id since we might skip some of them
    // in the middle
    current_breakpoint_id_ = result.front()->id;
    for (auto const *bp : result) {
        evaluated_ids_.emplace(bp->id);
    }
    return result;
}

void Debugger::start_breakpoint_evaluation() {
    evaluated_ids_.clear();
    current_breakpoint_id_ = std::nullopt;
    cached_signal_values_.clear();
}

bool Debugger::should_trigger(DebugBreakPoint *bp) {
    auto const &symbols = bp->trigger_symbols;
    // empty symbols means always trigger
    if (symbols.empty()) return true;
    bool should_trigger = false;
    for (auto const &symbol : symbols) {
        // if we haven't seen the value yet, definitely trigger it
        auto full_name = get_full_name(bp->instance_id, symbol);
        auto op_v = get_value(full_name);
        if (!op_v) {
            log_error(fmt::format("Unable to find signal {0} associated with breakpoint id {1}",
                                  full_name, bp->id));
            return true;
        }
        auto value = *op_v;
        if (bp->trigger_values.find(symbol) == bp->trigger_values.end() ||
            bp->trigger_values.at(symbol) != value) {
            should_trigger = true;
        }
        bp->trigger_values[symbol] = value;
    }
    return should_trigger;
}

std::string Debugger::get_full_name(uint64_t instance_id, const std::string &var_name) {
    std::string instance_name;
    {
        std::lock_guard guard(cached_instance_name_lock_);
        if (cached_instance_name_.find(instance_id) == cached_instance_name_.end()) {
            auto name = db_->get_instance_name(instance_id);
            if (name) {
                // need to remap to the full name
                instance_name = rtl_->get_full_name(*name);
                cached_instance_name_.emplace(instance_id, instance_name);
            }
        } else {
            instance_name = cached_instance_name_.at(instance_id);
        }
    }
    // exist lock here since this is local
    auto full_name = fmt::format("{0}.{1}", instance_name, var_name);
    return full_name;
}

std::optional<int64_t> Debugger::get_value(const std::string &signal_name) {
    // assume the name is already elaborated/mapped
    {
        std::lock_guard guard(cached_signal_values_lock_);
        if (cached_signal_values_.find(signal_name) != cached_signal_values_.end()) {
            return cached_signal_values_.at(signal_name);
        }
    }
    // need to actually get the value
    auto value = rtl_->get_value(signal_name);
    if (value) {
        std::lock_guard guard(cached_signal_values_lock_);
        cached_signal_values_.emplace(signal_name, *value);
        return *value;
    } else {
        return {};
    }
}

void Debugger::eval_breakpoint(DebugBreakPoint *bp, std::vector<bool> &result, uint32_t index) {
    const auto &bp_expr =
        evaluation_mode_ == EvaluationMode::BreakPointOnly ? bp->expr : bp->enable_expr;
    // get table values
    auto const &symbols = bp_expr->symbols();
    auto const bp_id = bp->id;
    std::unordered_map<std::string, int64_t> values;
    // just in case there are some context values need to pull in
    auto context_values = get_context_static_values(bp_id);
    // need to query through all its symbols
    for (auto const &symbol_name : symbols) {
        // if it is an context symbol
        if (context_values.find(symbol_name) != context_values.end()) {
            values.emplace(symbol_name, context_values.at(symbol_name));
        } else {
            // need to map these symbol names into the actual hierarchy
            // name
            auto full_name = get_full_name(bp->instance_id, symbol_name);
            auto v = rtl_->get_value(full_name);
            if (!v) break;
            values.emplace(symbol_name, *v);
        }
    }
    if (values.size() != symbols.size()) {
        // something went wrong with the querying symbol
        log_error(fmt::format("Unable to evaluate breakpoint {0}", bp_id));
    } else {
        auto eval_result = bp_expr->eval(values);
        auto trigger_result = should_trigger(bp);
        if (eval_result && trigger_result) {
            // trigger a breakpoint!
            result[index] = true;
        }
    }
}

}  // namespace hgdb