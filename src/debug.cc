#include "debug.hh"

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
    server_ = std::make_unique<DebugServer>();
    log_enabled_ = get_logging();
    // initialize the monitor
    monitor_ = Monitor([this](const std::string &name) { return this->get_value(name); });
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
    auto on_ = [this](const std::string &msg, uint64_t conn_id) { on_message(msg, conn_id); };
    server_thread_ = std::thread([on_, this]() {
        server_->set_on_message(on_);
        // need to get information about the port number
        auto port = get_port();
        is_running_ = true;
        server_->run(port);
        log_info(fmt::format("Debugging server started at :{0}", port));
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
        // to avoid multithreading overhead, only if there is more than 1 breakpoint
        if (bps.size() > 1) {
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

        } else {
            // directly evaluate it in the current thread to avoid creating threads
            // overhead
            this->eval_breakpoint(bps[0], hits, 0);
        }

        std::vector<const DebugBreakPoint *> result;
        result.reserve(bps.size());
        for (auto i = 0u; i < bps.size(); i++) {
            if (hits[i]) result.emplace_back(bps[i]);
        }

        if (!result.empty()) {
            // send the breakpoint hit information
            send_breakpoint_hit(result);
            // also send any breakpoint values
            send_monitor_values(true);
            // then pause the execution
            lock_.wait();
        }
    }
    send_monitor_values(false);
}

[[maybe_unused]] bool Debugger::is_verilator() {
    if (rtl_) {
        return rtl_->is_verilator();
    }
    return false;
}

Debugger::~Debugger() { server_thread_.join(); }

void Debugger::on_message(const std::string &message, uint64_t conn_id) {
    // server can only receives request
    auto req = Request::parse_request(message);
    if (req->status() != status_code::success) {
        // send back error message
        auto resp = GenericResponse(status_code::error, *req, req->error_reason());
        send_message(resp.str(log_enabled_), conn_id);
        return;
    }
    switch (req->type()) {
        case RequestType::connection: {
            // this is a connection request
            auto *r = reinterpret_cast<ConnectionRequest *>(req.get());
            handle_connection(*r, conn_id);
            break;
        }
        case RequestType::breakpoint: {
            auto *r = reinterpret_cast<BreakPointRequest *>(req.get());
            handle_breakpoint(*r, conn_id);
            break;
        }
        case RequestType::breakpoint_id: {
            auto *r = reinterpret_cast<BreakPointIDRequest *>(req.get());
            handle_breakpoint_id(*r, conn_id);
            break;
        }
        case RequestType::bp_location: {
            auto *r = reinterpret_cast<BreakPointLocationRequest *>(req.get());
            handle_bp_location(*r, conn_id);
            break;
        }
        case RequestType::command: {
            auto *r = reinterpret_cast<CommandRequest *>(req.get());
            handle_command(*r, conn_id);
            break;
        }
        case RequestType::debugger_info: {
            auto *r = reinterpret_cast<DebuggerInformationRequest *>(req.get());
            handle_debug_info(*r, conn_id);
            break;
        }
        case RequestType::path_mapping: {
            auto *r = reinterpret_cast<PathMappingRequest *>(req.get());
            handle_path_mapping(*r, conn_id);
            break;
        }
        case RequestType::evaluation: {
            auto *r = reinterpret_cast<EvaluationRequest *>(req.get());
            handle_evaluation(*r, conn_id);
            break;
        }
        case RequestType::option_change: {
            auto *r = reinterpret_cast<OptionChangeRequest *>(req.get());
            handle_option_change(*r, conn_id);
            break;
        }
        case RequestType::monitor: {
            auto *r = reinterpret_cast<MonitorRequest *>(req.get());
            handle_monitor(*r, conn_id);
            break;
        }
        case RequestType::error: {
            auto *r = reinterpret_cast<ErrorRequest *>(req.get());
            handle_error(*r, conn_id);
            break;
        }
    }
}

void Debugger::send_message(const std::string &message) {
    if (server_) {
        server_->send(message);
    }
}

void Debugger::send_message(const std::string &message, uint64_t conn_id) {
    if (server_) {
        server_->send(message, conn_id);
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
    if (!bp_info.condition.empty()) cond.append(" && " + bp_info.condition);
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
        validate_expr(breakpoints_.back().expr.get(), db_bp.id, *db_bp.instance_id);
        if (!breakpoints_.back().expr->correct()) [[unlikely]] {
            log_error("Unable to valid breakpoint expression: " + cond);
        }
        validate_expr(breakpoints_.back().enable_expr.get(), db_bp.id, *db_bp.instance_id);
        if (!breakpoints_.back().enable_expr->correct()) [[unlikely]] {
            log_error("Unable to valid breakpoint expression: " + cond);
        }
    } else {
        // update breakpoint entry
        for (auto &b : breakpoints_) {
            if (db_bp.id == b.id) {
                b.expr = std::make_unique<DebugExpression>(cond);
                validate_expr(b.expr.get(), db_bp.id, *db_bp.instance_id);
                if (!b.expr->correct()) [[unlikely]] {
                    log_error("Unable to valid breakpoint expression: " + cond);
                }
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

std::string Debugger::get_monitor_topic(uint64_t watch_id) {
    return fmt::format("watch-{0}", watch_id);
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

void Debugger::handle_connection(const ConnectionRequest &req, uint64_t conn_id) {
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

    // need to set the remap
    db_->set_src_mapping(req.path_mapping());

    if (success) {
        auto resp = GenericResponse(status_code::success, req);
        send_message(resp.str(log_enabled_), conn_id);
    } else {
        auto resp = GenericResponse(status_code::error, req,
                                    fmt::format("Unable to find {0}", db_filename));
        send_message(resp.str(log_enabled_), conn_id);
    }

    log_info("handle_connection finished");
}

void Debugger::handle_breakpoint(const BreakPointRequest &req, uint64_t conn_id) {
    if (!check_send_db_error(req.type(), conn_id)) return;

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
            send_message(error_response.str(log_enabled_), conn_id);
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
    send_message(success_resp.str(log_enabled_), conn_id);
}

void Debugger::handle_breakpoint_id(const BreakPointIDRequest &req, uint64_t conn_id) {
    if (!check_send_db_error(req.type(), conn_id)) return;
    // depends on whether it is add or remove
    auto const &bp_info = req.breakpoint();
    if (req.bp_action() == BreakPointRequest::action::add) {
        // need to query the db to get the actual breakpoint
        auto bp = db_->get_breakpoint(bp_info.id);
        if (!bp) {
            auto error_response =
                GenericResponse(status_code::error, req,
                                fmt::format("BP ({0}) is not a valid breakpoint", bp_info.id));
            send_message(error_response.str(log_enabled_), conn_id);
            return;
        }
        add_breakpoint(bp_info, *bp);
    } else {
        remove_breakpoint(bp_info);
    }
    // tell client we're good
    auto success_resp = GenericResponse(status_code::success, req);
    send_message(success_resp.str(log_enabled_), conn_id);
}

void Debugger::handle_bp_location(const BreakPointLocationRequest &req, uint64_t conn_id) {
    // if db is not connected correctly, abort
    if (!check_send_db_error(req.type(), conn_id)) return;

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
    send_message(resp.str(log_enabled_), conn_id);
}

void Debugger::handle_command(const CommandRequest &req, uint64_t) {
    switch (req.command_type()) {
        case CommandRequest::CommandType::continue_: {
            log_info("handle_command: continue_");
            evaluation_mode_ = EvaluationMode::BreakPointOnly;
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

void Debugger::handle_debug_info(const DebuggerInformationRequest &req, uint64_t conn_id) {
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
                        bps.emplace_back(BreakPoint{.id = bp_info->id,
                                                    .filename = bp_info->filename,
                                                    .line_num = bp_info->line_num,
                                                    .column_num = bp_info->column_num});
                        bps_.emplace_back(&bps.back());
                    }
                }
            }

            auto resp = DebuggerInformationResponse(bps_);
            req.set_token(resp);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }
        case DebuggerInformationRequest::CommandType::options: {
            // race conditions?
            auto options = get_options();
            auto options_map = options.get_options();
            auto resp = DebuggerInformationResponse(options_map);
            req.set_token(resp);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }
        case DebuggerInformationRequest::CommandType::status: {
            // race conditions?
            std::stringstream ss;
            ss << "Simulator: " << rtl_->get_simulator_name() << " "
               << rtl_->get_simulator_version() << std::endl;
            ss << "Command line arguments: ";
            auto const &argv = rtl_->get_argv();
            ss << util::join(argv.begin(), argv.end(), " ") << std::endl;
            ss << "Simulation paused: " << (is_running_.load() ? "true" : "false") << std::endl;
            auto resp = DebuggerInformationResponse(ss.str());
            req.set_token(resp);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }
    }
}

void Debugger::handle_path_mapping(const PathMappingRequest &req, uint64_t conn_id) {
    if (db_ && req.status() == status_code::success) [[likely]] {
        db_->set_src_mapping(req.path_mapping());
        auto resp = GenericResponse(status_code::success, req);
        send_message(resp.str(log_enabled_), conn_id);
    } else {
        auto resp = GenericResponse(status_code::error, req, req.error_reason());
        send_message(resp.str(log_enabled_), conn_id);
    }
}

// templated helpers
template <typename T>
bool get_symbol_values(std::unordered_map<std::string, ExpressionType> &values,
                       const std::unordered_set<std::string> &symbol_names,
                       const std::string &scope,
                       const std::vector<std::pair<T, Variable>> &variables,
                       const std::function<std::optional<int64_t>(const std::string &)> &get_value,
                       std::string &ec) {
    for (auto const &[front_var, var] : variables) {
        if (symbol_names.find(front_var.name) != symbol_names.end()) {
            auto v = var.is_rtl ? get_value(var.value) : util::stol(var.value);
            if (!v) {
                ec = "Unable get value for " + front_var.name;
                return false;
            }
            values.emplace(front_var.name, *v);
        }
    }
    // some values can be under some scope
    for (auto const &name : symbol_names) {
        if (values.find(name) != values.end()) continue;
        // has to be RTL signal
        auto v = get_value(scope.empty() ? name : fmt::format("{0}.{1}", scope, name));
        if (!v) {
            ec = "Unable to get value for " + name;
            return false;
        }
        values.emplace(name, *v);
    }
    return true;
}

// can't seem to simplify the function logic
// NOLINTNEXTLINE
void Debugger::handle_evaluation(const EvaluationRequest &req, uint64_t) {
    std::string error_reason = req.error_reason();
    // linux kernel style error handling
    if (db_ && req.status() == status_code::success) [[likely]] {
        // need to figure out if it is a valid instance name or just filename + line number
        auto const &scope = req.scope();
        DebugExpression expr(req.expression());
        std::unordered_map<std::string, int64_t> values;
        if (!expr.correct()) {
            error_reason = "Invalid expression";
            goto send_error;
        }
        auto const &symbol_names = expr.symbols();
        auto instance = db_->get_instance_id(scope);
        auto get_value_fn = [this](const std::string &name) { return get_value(name); };
        if (instance) {
            // this is instance scope
            auto generator_values = db_->get_generator_variable(*instance);
            if (!get_symbol_values(values, symbol_names, scope, generator_values, get_value_fn,
                                   error_reason)) {
                goto send_error;
            }
        } else if (scope.empty()) {
            if (!get_symbol_values<ContextVariable>(values, symbol_names, scope, {}, get_value_fn,
                                                    error_reason)) {
                goto send_error;
            }
        } else {
            // maybe it's a breakpoint id
            auto breakpoint_id = util::stoul(scope);
            if (!breakpoint_id) {
                error_reason = "Invalid scope " + scope;
                goto send_error;
            }
            auto instance_name = db_->get_instance_name_from_bp(*breakpoint_id);
            if (!instance_name) {
                error_reason = "Invalid scope " + scope;
                goto send_error;
            }
            // only have access to the scope values
            auto context_values = db_->get_context_variables(*breakpoint_id);
            if (!get_symbol_values(values, symbol_names, *instance_name, context_values,
                                   get_value_fn, error_reason)) {
                goto send_error;
            }
        }
        if (values.size() != symbol_names.size()) {
            error_reason = "Cannot find all required symbols";
            goto send_error;
        }
        auto value = expr.eval(values);
        EvaluationResponse eval_resp(scope, std::to_string(value));
        req.set_token(eval_resp);
        send_message(eval_resp.str(log_enabled_));
        return;
    }
send_error:
    auto resp = GenericResponse(status_code::error, req, error_reason);
    send_message(resp.str(log_enabled_));
}

void Debugger::handle_option_change(const OptionChangeRequest &req, uint64_t) {
    if (req.status() == status_code::success) {
        auto options = get_options();
        for (auto const &[name, value] : req.bool_values()) {
            options.set_option(name, value);
        }
        for (auto const &[name, value] : req.int_values()) {
            options.set_option(name, value);
        }
        for (auto const &[name, value] : req.str_values()) {
            options.set_option(name, value);
        }
        auto resp = GenericResponse(status_code::success, req);
        send_message(resp.str(log_enabled_));
    } else {
        auto resp = GenericResponse(status_code::error, req, req.error_reason());
        send_message(resp.str(log_enabled_));
    }
}

void Debugger::handle_monitor(const MonitorRequest &req, uint64_t conn_id) {
    if (req.status() == status_code::success) {
        // depends on whether it is an add or remove action
        if (req.action_type() == MonitorRequest::ActionType::add) {
            std::optional<std::string> full_name;
            if (req.breakpoint_id()) {
                auto bp = *req.breakpoint_id();
                full_name = db_->resolve_scoped_name_breakpoint(req.scope_name(), bp);

            } else {
                // instance id
                auto id = *req.instance_id();
                full_name = db_->resolve_scoped_name_instance(req.scope_name(), id);
            }
            if (!full_name || !rtl_->is_valid_signal(*full_name)) {
                auto resp = GenericResponse(status_code::error, req,
                                            "Unable to resolve " + req.scope_name());
                send_message(resp.str(log_enabled_));
                return;
            }
            auto track_id = monitor_.add_monitor_variable(*full_name, req.monitor_type());
            auto resp = GenericResponse(status_code::success, req);
            resp.set_value("track_id", track_id);
            // add topics
            auto topic = get_monitor_topic(track_id);
            this->server_->add_to_topic(topic, conn_id);

            send_message(resp.str(log_enabled_));
        } else {
            // it's remove
            auto track_id = req.track_id();
            monitor_.remove_monitor_variable(track_id);

            // remove topics
            auto topic = get_monitor_topic(track_id);
            this->server_->remove_from_topic(topic, conn_id);

            auto resp = GenericResponse(status_code::success, req);
            send_message(resp.str(log_enabled_));
        }

    } else {
        auto resp = GenericResponse(status_code::error, req, req.error_reason());
        send_message(resp.str(log_enabled_));
    }
}

void Debugger::handle_error(const ErrorRequest &req, uint64_t) {}

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

void Debugger::send_monitor_values(bool has_breakpoint) {
    //  optimize for no monitored value
    if (monitor_.empty()) [[likely]]
        return;
    auto values = monitor_.get_watched_values(has_breakpoint);
    for (auto const &[id, value] : values) {
        auto topic = get_monitor_topic(id);
        auto resp = MonitorResponse(id, value);
        send_message(resp.str(log_enabled_));
    }
}

util::Options Debugger::get_options() {
    util::Options options;
    options.add_option("single_thread_mode", &single_thread_mode_);
    options.add_option("log_enabled", &log_enabled_);
    return options;
}

bool Debugger::check_send_db_error(RequestType type, uint64_t conn_id) {
    if (!db_) {
        // need to send error response
        auto resp = GenericResponse(status_code::error, type,
                                    "Database is not initialized or is initialized incorrectly");
        send_message(resp.str(log_enabled_), conn_id);
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
    if (!current_breakpoint_id_) [[unlikely]] {
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
    validate_expr(step_over_breakpoint_.enable_expr.get(), step_over_breakpoint_.id,
                  step_over_breakpoint_.instance_id);
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

    std::vector<Debugger::DebugBreakPoint *> result{&breakpoints_[index]};

    // by default we generates as many breakpoints as possible to evaluate
    // this can be turned of by client's request (changed via option-change request)
    if (!single_thread_mode_) {
        // once we have a hit index, scanning down the list to see if we have more
        // hits if it shares the same fn/ln/cn tuple
        // matching criteria:
        // - same enable condition
        // - different instance id
        auto const &ref_bp = breakpoints_[index];
        auto const &target_expr = ref_bp.enable_expr->expression();
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
    // if not correct just always enable
    if (!bp_expr->correct()) return;
    // since at this point we have checked everything, just used the resolved name
    auto const &symbol_full_names = bp_expr->resolved_symbol_names();
    std::unordered_map<std::string, int64_t> values;
    for (auto const &[symbol_name, full_name] : symbol_full_names) {
        auto v = rtl_->get_value(full_name);
        if (!v) break;
        values.emplace(symbol_name, *v);
    }
    if (values.size() != symbol_full_names.size()) {
        // something went wrong with the querying symbol
        log_error(fmt::format("Unable to evaluate breakpoint {0}", bp->id));
    } else {
        auto eval_result = bp_expr->eval(values);
        auto trigger_result = should_trigger(bp);
        if (eval_result && trigger_result) {
            // trigger a breakpoint!
            result[index] = true;
        }
    }
}

void Debugger::validate_expr(DebugExpression *expr, uint32_t breakpoint_id, uint32_t instance_id) {
    auto context_static_values = get_context_static_values(breakpoint_id);
    expr->set_static_values(context_static_values);
    auto required_symbols = expr->get_required_symbols();
    for (auto const &symbol : required_symbols) {
        auto name = db_->resolve_scoped_name_breakpoint(symbol, instance_id);
        std::string full_name;
        if (name) [[likely]] {
            full_name = rtl_->get_full_name(*name);
        } else {
            // best effort
            full_name = rtl_->get_full_name(symbol);
        }
        // see if it's a valid signal
        bool valid = rtl_->is_valid_signal(full_name);
        if (!valid) {
            expr->set_error();
            return;
        }
        expr->set_resolved_symbol_name(symbol, full_name);
    }
}

}  // namespace hgdb