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

    // set up some call backs
    server_->set_on_call_client_disconnect([this]() {
        if (detach_after_disconnect_) detach();
    });

    // set vendor specific options
    set_vendor_initial_options();
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

    // set up the scheduler
    scheduler_ =
        std::make_unique<Scheduler>(rtl_.get(), db_.get(), single_thread_mode_, log_enabled_);

    // callbacks
    if (on_client_connected_) {
        (*on_client_connected_)(*db_);
    }
}

void Debugger::run() {
    auto on_ = [this](const std::string &msg, uint64_t conn_id) { on_message(msg, conn_id); };
    server_thread_ = std::thread([on_, this]() {
        server_->set_on_message(on_);
        // need to get information about the port number
        auto port = get_port();
        is_running_ = true;
        log_info(fmt::format("Debugging server started at :{0}", port));
        server_->run(port);
    });
    // block this thread until we receive the continue from user
    lock_.wait();
}

void Debugger::stop() {
    server_->stop();
    if (is_running_.load()) {
        // just detach it from the simulator
        detach();
    }
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
        auto bps = scheduler_->next_breakpoints();
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

void Debugger::set_option(const std::string &name, bool value) {
    auto options = get_options();
    options.set_option(name, value);
}

void Debugger::set_on_client_connected(
    const std::function<void(hgdb::DebugDatabaseClient &)> &func) {
    on_client_connected_ = func;
}

Debugger::~Debugger() { server_thread_.join(); }

void Debugger::detach() {
    // remove all the clock related callback
    // depends on whether it's verilator or not
    if (rtl_->is_verilator()) {
        rtl_->remove_call_back("eval_hgdb");
        log_info("Remove callback eval_hgdb");
    } else {
        std::set<std::string> callbacks;
        auto const callback_names = rtl_->callback_names();
        for (auto const &callback_name : callback_names) {
            if (callback_name.find("Monitor") != std::string::npos) {
                log_info("Remove callback " + callback_name);
                rtl_->remove_call_back(callback_name);
            }
        }
    }

    // set evaluation mode to normal
    if (scheduler_) scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::None);
    __sync_synchronize();

    // clear out inserted breakpoints

    // need to put this here to avoid compiler/cpu to reorder the code
    // such that lock is released before the breakpoints is cleared
    __sync_synchronize();

    if (is_running_.load()) {
        is_running_ = false;
        lock_.ready();
    }

    log_info("Debugger runtime detached since all clients have disconnected");
}

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

bool Debugger::has_cli_flag(const std::string &flag) {
    if (!rtl_) return false;
    const auto &argv = rtl_->get_argv();
    return std::any_of(argv.begin(), argv.end(), [&flag](const auto &v) { return v == flag; });
}

std::string Debugger::get_monitor_topic(uint64_t watch_id) {
    return fmt::format("watch-{0}", watch_id);
}

std::string Debugger::get_var_value(const Variable &var) {
    std::string value_str;
    if (var.is_rtl) {
        auto full_name = rtl_->get_full_name(var.value);
        if (!use_hex_str_) {
            auto value = rtl_->get_value(full_name);
            value_str = value ? std::to_string(*value) : error_value_str;
        } else {
            auto value = rtl_->get_str_value(full_name);
            value_str = value ? *value : error_value_str;
        }

    } else {
        value_str = var.value;
    }
    return value_str;
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
        auto clock_signals = util::get_clock_signals(rtl_.get(), db_.get());
        bool r = rtl_->monitor_signals(clock_signals, eval_hgdb_on_clk, this);
        if (!r || clock_signals.empty()) log_error("Failed to register evaluation callback");
    }

    // need to set the remap
    if (db_) db_->set_src_mapping(req.path_mapping());

    if (success) {
        auto resp = GenericResponse(status_code::success, req);
        send_message(resp.str(log_enabled_), conn_id);
        // set running to true
        is_running_ = true;
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
            scheduler_->add_breakpoint(bp_info, bp);
        }

        scheduler_->reorder_breakpoints();
    } else {
        // remove
        auto bps = db_->get_breakpoints(bp_info.filename, bp_info.line_num, bp_info.column_num);

        // notice that removal doesn't need reordering
        for (auto const &bp : bps) {
            scheduler_->remove_breakpoint(bp);
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
        scheduler_->add_breakpoint(bp_info, *bp);
    } else {
        scheduler_->remove_breakpoint(bp_info);
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
            scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::BreakPointOnly);
            lock_.ready();
            break;
        }
        case CommandRequest::CommandType::stop: {
            log_info("handle_command: stop");
            scheduler_->clear();
            scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::None);
            // we will unlock the lock during the stop step to ensure proper shutdown
            // of the runtime
            rtl_->finish_sim();
            stop();
            break;
        }
        case CommandRequest::CommandType::step_over: {
            log_info("handle_command: step_over");
            // change the mode into step through
            scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::StepOver);
            lock_.ready();
            break;
        }
        case CommandRequest::CommandType::reverse_continue: {
            log_info("handle_command: reverse_continue");
            scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::ReverseBreakpointOnly);
            lock_.ready();
            break;
        }
        case CommandRequest::CommandType::step_back: {
            log_info("handle_command: step_back");
            // change the mode into step back
            scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::StepBack);
            lock_.ready();
            break;
        }
    }
}

void Debugger::handle_debug_info(const DebuggerInformationRequest &req, uint64_t conn_id) {
    switch (req.command_type()) {
        case DebuggerInformationRequest::CommandType::breakpoints: {
            std::vector<BreakPoint> bps = scheduler_->get_current_breakpoints();
            std::vector<BreakPoint *> bps_;
            bps_.reserve(bps.size());
            for (auto &bp : bps) bps_.emplace_back(&bp);

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

void Debugger::handle_evaluation(const EvaluationRequest &req, uint64_t) {
    std::string error_reason = req.error_reason();
    // linux kernel style error handling
    auto send_error = [&error_reason, this, &req]() {
        auto resp = GenericResponse(status_code::error, req, error_reason);
        send_message(resp.str(log_enabled_));
    };

    if (db_ && req.status() == status_code::success) [[likely]] {
        // need to figure out if it is a valid instance name or just filename + line number
        auto const &scope = req.scope();
        DebugExpression expr(req.expression());
        if (!expr.correct()) {
            error_reason = "Invalid expression";
            send_error();
            return;
        }
        std::optional<uint32_t> instance_id;
        if (!req.is_context()) {
            if (std::all_of(scope.begin(), scope.end(), ::isdigit)) {
                instance_id = util::stoul(scope);
            } else {
                instance_id = db_->get_instance_id(scope);
            }
        }

        auto breakpoint_id = req.is_context() ? util::stoul(scope) : std::nullopt;
        util::validate_expr(rtl_.get(), db_.get(), &expr, breakpoint_id, instance_id);
        if (!expr.correct()) {
            error_reason = "Unable to resolve symbols";
            send_error();
            return;
        }

        std::unordered_map<std::string, int64_t> values;
        auto const &names = expr.resolved_symbol_names();
        for (auto const &[name, full_name] : names) {
            auto v = get_value(full_name);
            if (v) values.emplace(name, *v);
        }

        if (values.size() != names.size()) {
            error_reason = "Unable to get symbol values";
            send_error();
            return;
        }

        auto value = expr.eval(values);
        EvaluationResponse eval_resp(scope, std::to_string(value));
        req.set_token(eval_resp);
        send_message(eval_resp.str(log_enabled_));
        return;
    } else {
        send_error();
    }
}

void Debugger::handle_option_change(const OptionChangeRequest &req, uint64_t) {
    if (req.status() == status_code::success) {
        auto options = get_options();
        for (auto const &[name, value] : req.bool_values()) {
            log_info(fmt::format("option[{0}] set to {1}", name, value));
            options.set_option(name, value);
        }
        for (auto const &[name, value] : req.int_values()) {
            log_info(fmt::format("option[{0}] set to {1}", name, value));
            options.set_option(name, value);
        }
        for (auto const &[name, value] : req.str_values()) {
            log_info(fmt::format("option[{0}] set to {1}", name, value));
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
            std::string value_str = get_var_value(var);
            auto var_name = gen_var.name;
            scope.add_generator_value(var_name, value_str);
        }

        for (auto const &[gen_var, var] : context_values) {
            std::string value_str = get_var_value(var);
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
    options.add_option("detach_after_disconnect", &detach_after_disconnect_);
    options.add_option("use_hex_str", &use_hex_str_);
    return options;
}

void Debugger::set_vendor_initial_options() {
    // all the options already have initial values
    // this function is used to set
    if (rtl_) {
        if (rtl_->is_vcs()) {
            // we can't ctrl-c/d out of simv once it's paused
            // need to continue to simulation if client disconnected
            detach_after_disconnect_ = true;
        }
    }
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
    const auto &bp_expr = scheduler_->breakpoint_only() ? bp->expr : bp->enable_expr;
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

void Debugger::start_breakpoint_evaluation() {
    scheduler_->start_breakpoint_evaluation();
    cached_signal_values_.clear();
}

}  // namespace hgdb