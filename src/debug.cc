#include "debug.hh"

#include <filesystem>
#include <functional>
#include <thread>

#include "fmt/format.h"
#include "log.hh"
#include "perf.hh"
#include "util.hh"

namespace fs = std::filesystem;

constexpr auto DISABLE_BLOCKING_ENV = "DEBUG_DISABLE_BLOCKING";
constexpr auto DATABASE_FILENAME_ENV = "DEBUG_DATABASE_FILENAME";
constexpr auto DEBUG_LOGGING_ENV = "DEBUG_LOG";
constexpr auto DEBUG_PERF_COUNT = "DEBUG_PERF_COUNT";
constexpr auto DEBUG_BREAKPOINT_ENV = "DEBUG_BREAKPOINT{0}";
constexpr auto DEBUG_PERF_COUNT_LOG = "DEBUG_PERF_COUNT_LOG";

namespace hgdb {
Debugger::Debugger() : Debugger(nullptr) {}

Debugger::Debugger(std::shared_ptr<AVPIProvider> vpi) {
    // we create default namespace
    namespaces_.add_namespace(std::move(vpi));
    // initialize the webserver here
    server_ = std::make_unique<DebugServer>();
    log_enabled_ = get_logging();
    perf_count_ = get_perf_count();

    // set up some call backs
    server_->set_on_call_client_disconnect([this]() {
        if (detach_after_disconnect_) detach();
    });

    // set vendor specific options
    set_vendor_initial_options();
}

bool Debugger::initialize_db(const std::string &filename) {
    log_info(fmt::format("Debug database set to {0}", filename));
    initialize_db(create_symbol_table(filename));
    return db_ != nullptr;
}

void Debugger::initialize_db(std::unique_ptr<SymbolTableProvider> db) {
    // reset db_ for every new connection
    db_ = nullptr;
    if (!db) return;
    db_ = std::move(db);

    // set up the name mapping
    namespaces_.compute_instance_mapping(db_.get());

    // set up the scheduler
    scheduler_ =
        std::make_unique<Scheduler>(namespaces_, db_.get(), single_thread_mode_, log_enabled_);

    // callbacks
    if (on_client_connected_) {
        (*on_client_connected_)(*db_);
    }

    // setting methods to help the symbol table understand the design
    db_->set_get_symbol_value([this](const std::string &symbol_name) {
        return namespaces_.default_rtl()->get_value(symbol_name);
    });

    // setup breakpoints from env
    setup_init_breakpoint_from_env();
}

void Debugger::run() {
    // we also allow users to preload the database directly without a user connection
    // from the environment variable
    // this is only used for benchmark!
    preload_db_from_env();

    auto on_ = [this](const std::string &msg, uint64_t conn_id) { on_message(msg, conn_id); };
    server_thread_ = std::thread([on_, this]() {
        server_->set_on_message(on_);
        // need to get information about the port number
        auto port = get_port();
        is_running_ = true;
        log_info(fmt::format("Debugging server started at :{0}", port));
        server_->run(port);
    });
    // block this thread until we receive the continue command from user
    //
    // by default we block the execution. but if user desires, e.g. during a benchmark
    // we can skip the blocking
    // rocket chip doesn't like plus args. Need to
    bool disable_blocking = get_test_plus_arg(DISABLE_BLOCKING_ENV, true);
    if (!disable_blocking) [[likely]] {
        lock_.wait();
    }
}

void Debugger::stop() {
    server_->stop();
    if (is_running_.load()) {
        // just detach it from the simulator
        detach();
    }
}

void Debugger::eval() {
    perf::PerfCount perf("eval loop", perf_count_);
    // if we set to pause at posedge, need to do that at the very beginning!
    if (pause_at_posedge) [[unlikely]] {
        lock_.wait();
    }
    // the function that actually triggers breakpoints!
    // notice that there is a hidden race condition
    // when we trigger the breakpoint, the runtime (simulation side) will be paused via a lock.
    // however, the server side can still take breakpoint requests, hence modifying the
    // breakpoints_.
    log_info("Start breakpoint evaluation...");
    start_breakpoint_evaluation();  // clean the state and fetch values

    // main loop to fetch breakpoints
    while (true) {
        std::vector<DebugBreakPoint *> bps;
        {
            perf::PerfCount perf_get_bp("next breakpoints", perf_count_);
            bps = scheduler_->next_breakpoints();
        }

        if (bps.empty()) break;
        auto hits = eval_breakpoints(bps);

        std::vector<const DebugBreakPoint *> result;
        result.reserve(bps.size());
        for (auto i = 0u; i < bps.size(); i++) {
            if (hits[i]) result.emplace_back(bps[i]);
        }

        if (!result.empty()) {
            // send the breakpoint hit information
            send_breakpoint_hit(result);
            // also send any breakpoint values
            send_monitor_values(MonitorRequest::MonitorType::breakpoint);
            // then pause the execution
            lock_.wait();
        }
    }

    send_monitor_values(MonitorRequest::MonitorType::clock_edge);
}

[[maybe_unused]] bool Debugger::is_verilator() {
    if (!namespaces_.empty()) {
        return namespaces_.default_rtl()->is_verilator();
    }
    return false;
}

std::vector<RTLSimulatorClient *> Debugger::rtl_clients() const {
    std::vector<RTLSimulatorClient *> result;
    result.reserve(namespaces_.size());
    for (const auto &ns : namespaces_) {
        result.emplace_back(ns->rtl.get());
    }
    return result;
}

void Debugger::set_option(const std::string &name, bool value) {
    auto options = get_options();
    options.set_option(name, value);
}

void Debugger::set_on_client_connected(
    const std::function<void(hgdb::SymbolTableProvider &)> &func) {
    on_client_connected_ = func;
}

Debugger::~Debugger() {
    if (server_thread_.joinable()) server_thread_.join();
}

void Debugger::detach() {
    // remove all the clock related callback
    // depends on whether it's verilator or not
    // always use the first one
    auto *rtl = namespaces_.default_rtl();
    if (rtl->is_verilator()) {
        rtl->remove_call_back("eval_hgdb");
        log_info("Remove callback eval_hgdb");
    } else {
        std::set<std::string> callbacks;
        auto const callback_names = rtl->callback_names();
        for (auto const &callback_name : callback_names) {
            if (callback_name.find("Monitor") != std::string::npos) {
                log_info("Remove callback " + callback_name);
                rtl->remove_call_back(callback_name);
            }
        }
    }

    // set evaluation mode to normal
    if (scheduler_) scheduler_->set_evaluation_mode(Scheduler::EvaluationMode::None);

    // print out perf if enabled
    if (perf_count_) {
        auto filename = get_value_plus_arg(DEBUG_PERF_COUNT_LOG, true);
        perf::PerfCount::print_out(filename ? *filename : "");
    }

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
    // server can only receive request
    auto req = Request::parse_request(message);
    if (req->status() != status_code::success) {
        // send back error message
        auto resp = GenericResponse(status_code::error, *req, req->error_reason());
        send_message(resp.str(log_enabled_), conn_id);
        return;
    }
    log_info("Start handling " + to_string(req->type()));
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
        case RequestType::set_value: {
            auto *r = reinterpret_cast<SetValueRequest *>(req.get());
            handle_set_value(*r, conn_id);
            break;
        }
        case RequestType::error: {
            auto *r = reinterpret_cast<ErrorRequest *>(req.get());
            handle_error(*r, conn_id);
            break;
        }
        case RequestType::symbol: {
            auto *r = reinterpret_cast<SymbolRequest *>(req.get());
            handle_symbol(*r, conn_id);
            break;
        }
        case RequestType::data_breakpoint: {
            auto *r = reinterpret_cast<DataBreakpointRequest *>(req.get());
            handle_data_breakpoint(*r, conn_id);
            break;
        }
    }
    log_info("Done handling " + to_string(req->type()));
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
    auto port_str = get_value_plus_arg("DEBUG_PORT");
    if (!port_str) return default_port_num;
    uint16_t value;
    try {
        value = std::stoul(*port_str);
    } catch (const std::invalid_argument &) {
        value = default_port_num;
    } catch (const std::out_of_range &) {
        value = default_port_num;
    }
    return value;
}

std::optional<std::string> Debugger::get_value_plus_arg(const std::string &arg_name,
                                                        bool check_env) {
    if (namespaces_.empty()) return std::nullopt;
    auto const &args = namespaces_.default_rtl()->get_argv();
    auto const plus_arg = fmt::format("+{0}=", arg_name);
    for (auto const &arg : args) {
        if (arg.find(plus_arg) != std::string::npos) {
            auto value = arg.substr(plus_arg.size());
            return value;
        }
    }
    // check env as well
    return util::getenv(arg_name);
}

bool Debugger::get_test_plus_arg(const std::string &arg_name, bool check_env) {
    if (namespaces_.empty()) return false;
    auto const &args = namespaces_.default_rtl()->get_argv();
    // we check env the last since we allow plus args to override env values
    auto plus_arg = "+" + arg_name;
    auto r = std::any_of(args.begin(), args.end(),
                         [&plus_arg](const auto &arg) { return arg == plus_arg; });

    // check env as well if allowed
    if (!r && check_env) {
        if (util::getenv(arg_name)) {
            return true;
        }
    }
    return r;
}

bool Debugger::get_logging() {
    auto logging = get_test_plus_arg(DEBUG_LOGGING_ENV, true);
    return logging ? true : default_logging;  // NOLINT
}

bool Debugger::get_perf_count() { return get_test_plus_arg(DEBUG_PERF_COUNT, true); }

void Debugger::log_error(const std::string &msg) { log::log(log::log_level::error, msg); }

void Debugger::log_info(const std::string &msg) const {
    if (log_enabled_) {
        log::log(log::log_level::info, msg);
    }
}

bool Debugger::has_cli_flag(const std::string &flag) {
    if (namespaces_.empty()) return false;
    auto const &argv = namespaces_.default_rtl()->get_argv();
    return std::any_of(argv.begin(), argv.end(), [&flag](const auto &v) { return v == flag; });
}

std::string Debugger::get_monitor_topic(uint64_t watch_id) {
    return fmt::format("watch-{0}", watch_id);
}

std::string value_to_str(std::optional<int64_t> value, bool use_hex, uint32_t width = 0) {
    if (!value) {
        return Debugger::error_value_str;
    }
    if (use_hex) {
        std::string format;
        if (width == 0) {
            format = "0x{0:X}";
        } else if (width == 1) {
            // this is bit format
            format = "{0}";
        } else {
            width = (width % 4 == 0) ? width / 4 : (width / 4 + 1);
            format = fmt::format("0x{{0:0{0}X}}", width);
        }
        return fmt::format(format.c_str(), *value);

    } else {
        return fmt::format("{0}", *value);
    }
}

// NOLINTNEXTLINE
std::string Debugger::get_value_str(uint32_t ns_id, const std::string &rtl_name, bool is_rtl,
                                    bool use_delay) {
    std::string value_str;
    if (is_rtl) {
        auto &rtl = namespaces_[ns_id]->rtl;
        auto *handle = rtl->get_handle(rtl_name);
        uint32_t width = 0;
        // width is only used for hex string
        if (use_hex_str_) [[unlikely]] {
            auto width_opt = rtl->get_signal_width(handle);
            width = width_opt ? *width_opt : 0u;
        }

        if (use_delay) {
            if (delayed_variables_.find(handle) == delayed_variables_.end()) [[unlikely]] {
                log_error("Internal error on handling delayed variables");
                value_str = error_value_str;
            } else {
                value_str = value_to_str(delayed_variables_.at(handle).value, use_hex_str_, width);
            }
        } else {
            auto value = rtl->get_value(handle);
            value_str = value_to_str(value, use_hex_str_, width);
        }
    } else {
        value_str = rtl_name;
    }
    return value_str;
}

std::optional<std::string> Debugger::resolve_var_name(
    uint32_t ns_id, const std::string &var_name, const std::optional<uint64_t> &instance_id,
    const std::optional<uint64_t> &breakpoint_id) {
    std::optional<std::string> full_name;
    if (breakpoint_id) {
        auto bp = *breakpoint_id;
        full_name = db_->resolve_scoped_name_breakpoint(var_name, bp);
    } else if (instance_id) {
        // instance id
        auto id = *instance_id;
        full_name = db_->resolve_scoped_name_instance(var_name, id);
    } else {
        // just the normal name
        full_name = var_name;
    }
    auto &rtl = namespaces_[ns_id]->rtl;
    if (full_name && !rtl->is_valid_signal(*full_name)) {
        full_name = std::nullopt;
    }

    return full_name;
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
    if (success) {
        add_cb_clocks();
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

    // depends on whether it is "add" or "remove"
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

        scheduler_->reorder_breakpoints();
    } else {
        // remove
        auto bps = db_->get_breakpoints(bp_info.filename, bp_info.line_num, bp_info.column_num);

        // notice that removal doesn't need reordering
        for (auto const &bp : bps) {
            scheduler_->remove_breakpoint(bp, DebugBreakPoint::Type::normal);
        }
    }
    // tell client we're good
    auto success_resp = GenericResponse(status_code::success, req);
    send_message(success_resp.str(log_enabled_), conn_id);
}

void Debugger::handle_breakpoint_id(const BreakPointIDRequest &req, uint64_t conn_id) {
    if (!check_send_db_error(req.type(), conn_id)) return;
    // depends on whether it is "add" or "remove"
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
        scheduler_->remove_breakpoint(bp_info, DebugBreakPoint::Type::normal);
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

void Debugger::handle_command(const CommandRequest &req, uint64_t conn_id) {
    status_code status = status_code::success;
    std::string error;

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
            namespaces_.default_rtl()->finish_sim();
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
        case CommandRequest::CommandType::jump: {
            log_info(fmt::format("handle_command: jump ({0})", req.time()));
            auto res = namespaces_.default_rtl()->rewind(req.time(), scheduler_->clock_handles());
            if (!res) {
                status = status_code::error;
                error = "Underlying RTL simulator does not support rewind";
                log_error(error);
            }
            lock_.ready();
            break;
        }
    }

    if (status == status_code::success) {
        auto resp = GenericResponse(status_code::success, req);
        send_message(resp.str(log_enabled_), conn_id);
    } else {
        auto resp = GenericResponse(status_code::error, req, error);
        send_message(resp.str(log_enabled_), conn_id);
    }
}

void Debugger::handle_debug_info(const DebuggerInformationRequest &req, uint64_t conn_id) {
    switch (req.command_type()) {
        case DebuggerInformationRequest::CommandType::breakpoints: {
            auto bps = scheduler_->get_current_breakpoints();
            std::vector<const DebugBreakPoint *> bps_;
            bps_.reserve(bps.size());
            for (auto &bp : bps) bps_.emplace_back(bp);

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
            auto *rtl = namespaces_.default_rtl();
            ss << "Simulator: " << rtl->get_simulator_name() << " " << rtl->get_simulator_version()
               << std::endl;
            ss << "Command line arguments: ";
            auto const &argv = rtl->get_argv();
            ss << util::join(argv.begin(), argv.end(), " ") << std::endl;
            ss << "Simulation paused: " << (is_running_.load() ? "true" : "false") << std::endl;
            auto resp = DebuggerInformationResponse(ss.str());
            req.set_token(resp);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }
        case DebuggerInformationRequest::CommandType::design: {
            // this basically lists the top mapping so the client will know the
            // where the design is
            // TODO: Fix this
            auto *rtl = namespaces_.default_rtl();
            auto mapping = rtl->get_top_mapping();
            auto resp = DebuggerInformationResponse(mapping);
            req.set_token(resp);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }
        case DebuggerInformationRequest::CommandType::filename: {
            auto filenames = db_->get_filenames();
            auto resp = DebuggerInformationResponse(filenames);
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

void find_matching_namespace_validate(DebugExpression &expr, SymbolTableProvider *db,
                                      std::optional<uint32_t> breakpoint_id,
                                      std::optional<uint32_t> instance_id,
                                      DebuggerNamespaceManager &namespaces_) {
    bool matched = false;
    for (auto const &ns_ : namespaces_) {
        util::validate_expr(ns_->rtl.get(), db, &expr, breakpoint_id, instance_id);
        if (expr.correct()) {
            matched = true;
            break;
        } else {
            expr.clear();
        }
    }
    if (!matched) {
        // this is an error
        expr.set_error();
    }
}

DebuggerNamespace *get_namespace(std::optional<uint32_t> instance_id,
                                 std::optional<uint32_t> breakpoint_id,
                                 DebuggerNamespaceManager &namespaces_, SymbolTableProvider *db) {
    DebuggerNamespace *ns = nullptr;
    if (instance_id) {
        auto instance_name = db->get_instance_name(*instance_id);
        if (instance_name) {
            // FIXME:
            //   add namespace ID to the request
            auto namespaces = namespaces_.get_namespaces(*instance_name);
            if (!namespaces.empty()) {
                ns = namespaces[0];
            }
        }
    }
    return ns;
}

// NOLINTNEXTLINE
void Debugger::handle_evaluation(const EvaluationRequest &req, uint64_t conn_id) {
    std::string error_reason = req.error_reason();
    auto send_error = [&error_reason, this, &req, conn_id]() {
        auto resp = GenericResponse(status_code::error, req, error_reason);
        send_message(resp.str(log_enabled_), conn_id);
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
        auto *ns = get_namespace(instance_id, breakpoint_id, namespaces_, db_.get());

        if (!ns) ns = namespaces_.default_namespace();

        // if we don't have any breakpoint id and instance id provided
        // it's free for all
        if (instance_id || breakpoint_id) {
            util::validate_expr(ns->rtl.get(), db_.get(), &expr, breakpoint_id, instance_id);
        } else {
            find_matching_namespace_validate(expr, db_.get(), breakpoint_id, instance_id,
                                             namespaces_);
        }

        if (!expr.correct()) {
            error_reason = "Unable to resolve symbols";
            send_error();
            return;
        }

        auto res = set_expr_values(ns ? ns->id : 0, &expr, instance_id ? *instance_id : 0);

        if (!res) {
            error_reason = "Unable to get symbol values";
            send_error();
            return;
        }

        auto value = expr.eval();
        EvaluationResponse eval_resp(scope, std::to_string(value));
        req.set_token(eval_resp);
        send_message(eval_resp.str(log_enabled_), conn_id);
        return;
    } else {
        send_error();
    }
}

void Debugger::handle_option_change(const OptionChangeRequest &req, uint64_t conn_id) {
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
        send_message(resp.str(log_enabled_), conn_id);
    } else {
        auto resp = GenericResponse(status_code::error, req, req.error_reason());
        send_message(resp.str(log_enabled_), conn_id);
    }
}

void Debugger::handle_monitor(const MonitorRequest &req, uint64_t conn_id) {
    auto send_error = [this, &req, conn_id](const std::string &reason) {
        auto resp = GenericResponse(status_code::error, req, reason);
        send_message(resp.str(log_enabled_), conn_id);
    };

    if (req.status() == status_code::success) {
        // depends on whether it is an add or remove action
        uint32_t ns_id;
        if (req.namespace_id()) {
            ns_id = *req.namespace_id();
        } else {
            std::optional<std::string> instance_name;
            if (req.instance_id()) {
                instance_name = db_->get_instance_name(*req.instance_id());
            } else if (req.breakpoint_id()) {
                instance_name = db_->get_instance_name_from_bp(*req.breakpoint_id());
            }
            std::vector<DebuggerNamespace *> namespaces;
            if (instance_name) {
                auto nss = namespaces_.get_namespaces(*instance_name);
                namespaces = nss;
            }
            if (namespaces.size() != 1) {
                send_error("Unable to determine RTL namespace");
                return;
            }
            ns_id = namespaces[0]->id;
        }

        auto &monitor = *namespaces_[ns_id]->monitor;
        if (req.action_type() == MonitorRequest::ActionType::add) {
            std::optional<std::string> full_name =
                resolve_var_name(ns_id, req.var_name(), req.instance_id(), req.breakpoint_id());
            if (!full_name) {
                send_error("Unable to resolve " + req.var_name());
                return;
            }
            auto track_id = monitor.add_monitor_variable(*full_name, req.monitor_type());
            auto resp = GenericResponse(status_code::success, req);
            resp.set_value("track_id", track_id);
            resp.set_value("namespace_id", ns_id);
            // add topics
            auto topic = get_monitor_topic(track_id);
            this->server_->add_to_topic(topic, conn_id);

            send_message(resp.str(log_enabled_), conn_id);
        } else {
            // it's remove
            auto track_id = req.track_id();
            monitor.remove_monitor_variable(track_id);

            // remove topics
            auto topic = get_monitor_topic(track_id);
            this->server_->remove_from_topic(topic, conn_id);

            auto resp = GenericResponse(status_code::success, req);
            send_message(resp.str(log_enabled_), conn_id);
        }

    } else {
        send_error(req.error_reason());
    }
}

void Debugger::handle_set_value(const SetValueRequest &req, uint64_t conn_id) {  // NOLINT
    log_info(fmt::format("handle set value {0} = {1}", req.var_name(), req.value()));

    if (req.status() == status_code::success) {
        auto const ns_id = req.namespace_id() ? *req.namespace_id() : namespaces_.default_id();
        auto &rtl = namespaces_[ns_id]->rtl;
        std::optional<std::string> full_name =
            resolve_var_name(ns_id, req.var_name(), req.instance_id(), req.breakpoint_id());
        if (!full_name) {
            auto resp =
                GenericResponse(status_code::error, req, "Unable to resolve " + req.var_name());
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }
        // need to set the value
        auto res = rtl->set_value(*full_name, req.value());
        if (res) {
            // we need to remove cached value
            if (use_signal_cache_) {
                auto *handle = rtl->get_handle(*full_name);
                std::lock_guard guard(cached_signal_values_lock_);
                if (cached_signal_values_.find(handle) != cached_signal_values_.end()) {
                    cached_signal_values_.erase(handle);
                }
            }
            auto resp = GenericResponse(status_code::success, req);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        } else {
            auto resp =
                GenericResponse(status_code::error, req, "Unable to set value for " + *full_name);
            send_message(resp.str(log_enabled_), conn_id);
            return;
        }

    } else {
        auto resp = GenericResponse(status_code::error, req, req.error_reason());
        send_message(resp.str(log_enabled_), conn_id);
    }
}

void Debugger::handle_error(const ErrorRequest &req, uint64_t) {}

void Debugger::handle_symbol(const SymbolRequest &, uint64_t) {
    // we don't deal with symbol stuff in the debugger
}

// NOLINTNEXTLINE
void Debugger::handle_data_breakpoint(const DataBreakpointRequest &req, uint64_t conn_id) {
    // TODO: add namespace id to req
    const auto ns_id = 0;
    switch (req.action()) {
        case DataBreakpointRequest::Action::clear: {
            scheduler_->clear_data_breakpoints();
            auto resp = GenericResponse(status_code::success, req);
            send_message(resp.str(log_enabled_), conn_id);
            log_info("data breakpoint cleared");
            break;
        }
        case DataBreakpointRequest::Action::info:
        case DataBreakpointRequest::Action::add: {
            bool dry_run = req.action() == DataBreakpointRequest::Action::info;
            auto db_bp = db_->get_breakpoint(req.breakpoint_id());
            if (!db_bp) {
                auto error = GenericResponse(status_code::error, req, "Invalid breakpoint id");
                send_message(error.str(log_enabled_), conn_id);
                return;
            }
            // this is not very efficient, but good enough for now
            auto bp_ids = db_->get_assigned_breakpoints(req.var_name(), req.breakpoint_id());
            auto inst_name = db_->get_instance_name_from_bp(req.breakpoint_id());
            if (bp_ids.empty() || !inst_name) {
                auto err = GenericResponse(status_code::error, req, "Invalid data breakpoint");
                send_message(err.str(log_enabled_), conn_id);
                return;
            }
            std::unordered_set<std::string> var_names;
            for (auto const &iter : bp_ids) {
                auto full_name = fmt::format("{0}.{1}", *inst_name, std::get<1>(iter));
                var_names.emplace(namespaces_[ns_id]->rtl->get_full_name(full_name));
            }

            for (auto const &[id, var_name, data_condition] : bp_ids) {
                auto bp_opt = db_->get_breakpoint(id);
                if (!bp_opt) {
                    auto error = GenericResponse(status_code::error, req, "Invalid breakpoint id");
                    send_message(error.str(log_enabled_), conn_id);
                    return;
                }
                // merge data_condition
                std::string bp_condition;
                if (req.condition().empty())
                    bp_condition = data_condition;
                else if (bp_condition.empty())
                    bp_condition = req.condition();
                else
                    // merge these two
                    bp_condition = fmt::format("{0} && {1}", req.condition(), data_condition);
                auto *bp =
                    scheduler_->add_data_breakpoint(var_name, bp_condition, *bp_opt, dry_run);
                if (!bp) {
                    auto error =
                        GenericResponse(status_code::error, req,
                                        "Invalid data breakpoint expression/data_condition");
                    send_message(error.str(log_enabled_), conn_id);
                    return;
                }

                // they share the same variable
                // notice that in case some breakpoints got deleted, we need to get it from the
                // monitor itself
                auto &monitor = namespaces_[ns_id]->monitor;
                auto value_ptr =
                    monitor->get_watched_value_ptr(var_names, MonitorRequest::MonitorType::data);
                if (!value_ptr) {
                    value_ptr = std::make_shared<std::optional<int64_t>>();
                }

                // add it to the monitor
                if (!dry_run) {
                    auto watched = monitor->is_monitored(bp->full_rtl_handle,
                                                         MonitorRequest::MonitorType::data);
                    if (!watched) {
                        bp->watch_id = monitor->add_monitor_variable(
                            bp->full_rtl_name, MonitorRequest::MonitorType::data, value_ptr);
                        log_info(fmt::format("Added watch variable with ID {0}", bp->watch_id));
                    }
                }
            }

            // sort the breakpoints
            scheduler_->reorder_breakpoints();

            // tell client we're good
            auto success_resp = GenericResponse(status_code::success, req);
            send_message(success_resp.str(log_enabled_), conn_id);
            break;
        }
        case DataBreakpointRequest::Action::remove: {
            auto watch_id = scheduler_->remove_data_breakpoint(req.breakpoint_id());
            // remove from monitor as well
            if (watch_id) {
                auto &monitor = namespaces_[ns_id]->monitor;
                monitor->remove_monitor_variable(*watch_id);
                log_info(fmt::format("Remove watch variable with ID {0}", *watch_id));
            }
            // tell client we're good
            auto success_resp = GenericResponse(status_code::success, req);
            send_message(success_resp.str(log_enabled_), conn_id);
            break;
        }
    }
}

std::vector<std::pair<std::string, std::string>> resolve_generator_name(std::string rtl_name_base,
                                                                        const std::string &var_name,
                                                                        uint32_t instance_id,
                                                                        RTLSimulatorClient *rtl,
                                                                        SymbolTableProvider *db) {
    if (!rtl->is_absolute_path(rtl_name_base)) {
        auto v = db->resolve_scoped_name_instance(rtl_name_base, instance_id);
        if (v) rtl_name_base = *v;
    }
    auto var_names = rtl->resolve_rtl_variable(var_name, rtl_name_base);
    return var_names;
}

std::vector<std::pair<std::string, std::string>> resolve_context_name(std::string rtl_name_base,
                                                                      const std::string &var_name,
                                                                      uint32_t bp_id,
                                                                      RTLSimulatorClient *rtl,
                                                                      SymbolTableProvider *db) {
    if (!rtl->is_absolute_path(rtl_name_base)) {
        auto v = db->resolve_scoped_name_breakpoint(rtl_name_base, bp_id);
        if (v) rtl_name_base = *v;
    }
    auto var_names = rtl->resolve_rtl_variable(var_name, rtl_name_base);
    return var_names;
}

void Debugger::send_breakpoint_hit(const std::vector<const DebugBreakPoint *> &bps) {
    // we send it here to avoid a round trip of client asking for context and send it
    // back
    auto const *first_bp = bps.front();
    BreakPointResponse resp(namespaces_.default_rtl()->get_simulation_time(), first_bp->filename,
                            first_bp->line_num, first_bp->column_num);
    for (auto const *bp : bps) {
        // first need to query all the values
        auto bp_id = bp->id;
        auto generator_values = db_->get_generator_variable(bp->instance_id);
        auto context_values = db_->get_context_variables(bp_id);
        auto bp_ptr = db_->get_breakpoint(bp_id);
        auto instance_name = db_->get_instance_name_from_bp(bp_id);
        auto instance_name_str = instance_name ? *instance_name : "";

        BreakPointResponse::Scope scope(bp->instance_id, instance_name_str, bp_id);
        switch (bp->type) {
            case DebugBreakPoint::Type::data:
                scope.bp_type = "data";
                break;
            case DebugBreakPoint::Type::normal:
            default:
                scope.bp_type = "normal";
                break;
        }

        auto *rtl = namespaces_[bp->ns_id]->rtl.get();
        using namespace std::string_literals;
        for (auto const &[gen_var, var] : generator_values) {
            // maybe need to resolve the name based on the variable
            auto var_names =
                resolve_generator_name(var.value, gen_var.name, bp->instance_id, rtl, db_.get());
            for (auto const &[front_name, rtl_name] : var_names) {
                std::string value_str = get_value_str(bp->ns_id, rtl_name, var.is_rtl);
                scope.add_generator_value(front_name, value_str);
            }
        }

        for (auto const &[ctx_var, var] : context_values) {
            auto var_names = resolve_context_name(var.value, ctx_var.name, bp->id, rtl, db_.get());
            for (auto const &[front_name, rtl_name] : var_names) {
                using VariableType = SymbolTableProvider::VariableType;
                std::string value_str =
                    get_value_str(bp->ns_id, rtl_name, var.is_rtl,
                                  static_cast<VariableType>(ctx_var.type) == VariableType::delay);
                scope.add_local_value(front_name, value_str);
            }
        }
        resp.add_scope(scope);
    }

    auto str = resp.str(log_enabled_);
    send_message(str);
}

void Debugger::send_monitor_values(MonitorRequest::MonitorType type) {
    //  optimize for no monitored value
    for (const auto &ns : namespaces_) {
        auto &monitor = *ns->monitor;
        if (monitor.empty()) [[likely]]
            continue;
        auto values = monitor.get_watched_values(type);
        for (auto const &[id, value] : values) {
            auto topic = get_monitor_topic(id);
            auto value_str = value_to_str(value, use_hex_str_);
            auto resp = MonitorResponse(id, ns->id, value_str);
            send_message(resp.str(log_enabled_));
        }
    }
}

util::Options Debugger::get_options() {
    util::Options options;
    options.add_option("single_thread_mode", &single_thread_mode_);
    options.add_option("log_enabled", &log_enabled_);
    options.add_option("detach_after_disconnect", &detach_after_disconnect_);
    options.add_option("use_hex_str", &use_hex_str_);
    options.add_option("pause_at_posedge", &pause_at_posedge);
    options.add_option("perf_count", &perf_count_);
    options.add_option("use_signal_cache", &use_signal_cache_);
    return options;
}

void Debugger::set_vendor_initial_options() {
    // all the options already have initial values
    // this function is used to set
    if (!namespaces_.empty()) {
        if (namespaces_.default_rtl()->is_vcs()) {
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
    for (auto const &[symbol, handle] : symbols) {
        // if we haven't seen the value yet, definitely trigger it
        auto op_v = get_signal_value(bp->ns_id, handle);
        if (!op_v) {
            auto full_name = get_full_name(bp->ns_id, bp->instance_id, symbol);
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

std::string Debugger::get_full_name(uint32_t ns_id, uint64_t instance_id,
                                    const std::string &var_name) {
    std::string instance_name;
    {
        std::lock_guard guard(cached_instance_name_lock_);
        if (cached_instance_name_.find(instance_id) == cached_instance_name_.end()) {
            auto name = db_->get_instance_name(instance_id);
            if (name) {
                // need to remap to the full name
                instance_name = namespaces_[ns_id]->rtl->get_full_name(*name);
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

std::optional<int64_t> Debugger::get_signal_value(uint32_t ns_id, vpiHandle handle,
                                                  bool use_delayed) {
    // assume the name is already elaborated/mapped
    if (use_signal_cache_) [[unlikely]] {
        std::lock_guard guard(cached_signal_values_lock_);
        if (cached_signal_values_.find(handle) != cached_signal_values_.end()) {
            return cached_signal_values_.at(handle);
        }
    }
    // need to actually get the value
    std::optional<int64_t> value;
    if (use_delayed && delayed_variables_.find(handle) != delayed_variables_.end()) [[unlikely]] {
        return delayed_variables_.at(handle).value;
    }

    value = namespaces_[ns_id]->rtl->get_value(handle);

    if (!value)
        log_info(fmt::format("Failed to obtain RTL value for handle id 0x{0}",
                             static_cast<void *>(handle)));

    if (value) {
        if (use_signal_cache_) {
            std::lock_guard guard(cached_signal_values_lock_);
            cached_signal_values_.emplace(handle, *value);
        }

        return *value;
    } else {
        return {};
    }
}

bool Debugger::eval_breakpoint(DebugBreakPoint *bp) {
    const auto &bp_expr = scheduler_->breakpoint_only() ? bp->expr : bp->enable_expr;
    if (!bp_expr->correct()) return false;
    bool res;
    {
        perf::PerfCount count("get_rtl_values", perf_count_);
        res = set_expr_values(bp->ns_id, bp_expr.get(), bp->instance_id);
    }
    if (!res) [[unlikely]] {
        // something went wrong with the querying symbol
        log_error(fmt::format("Unable to evaluate breakpoint {0}", bp->id));
    } else {
        long eval_result;
        {
            perf::PerfCount count("eval breakpoint", perf_count_);
            eval_result = bp_expr->eval();
        }

        auto *ns = namespaces_[bp->ns_id];
        auto trigger_result = should_trigger(bp);
        bool data_bp = true;
        bool enabled = eval_result && trigger_result;
        if (bp->type == DebugBreakPoint::Type::data && enabled) {
            auto [changed, _] = ns->monitor->var_changed(bp->watch_id);
            data_bp = changed;
        }
        if (enabled && data_bp) {
            // trigger a breakpoint!
            return true;
        }
    }
    return false;
}

std::vector<bool> Debugger::eval_breakpoints(const std::vector<DebugBreakPoint *> &bps) {
    std::vector<bool> hits(bps.size());
    std::fill(hits.begin(), hits.end(), false);
    // most of the time is on getting simulation values, so using many threads
    // doesn't make any sense
    const static auto processor_count = 2;
    auto constexpr minimum_batch_size = 16;
    auto const batch_min_size = processor_count * minimum_batch_size;
    const static auto commercial =
        namespaces_.default_rtl()->is_vcs() || namespaces_.default_rtl()->is_xcelium();
    if (bps.size() > batch_min_size && !commercial) {
        // multi-threading for two threads
        perf::PerfCount perf_bp_threads("eval bp threads", perf_count_);
        std::vector<std::thread> threads;
        threads.reserve(processor_count);
        auto batch_size = bps.size() / processor_count;
        for (auto i = 0u; i < processor_count; i++) {
            // lock-free map
            auto start = i * batch_size;
            auto end = (i == (processor_count - 1)) ? bps.size() : (i + 1) * batch_size;
            threads.emplace_back(std::thread([start, end, &bps, &hits, this]() {
                this->eval_breakpoint(bps, hits, start, end);
            }));
        }
        for (auto &t : threads) t.join();

    } else {
        // directly evaluate it in the current thread to avoid creating threads
        // overhead
        perf::PerfCount perf_bp_threads("eval bp single thread", perf_count_);
        this->eval_breakpoint(bps, hits, 0, bps.size());
    }
    return hits;
}

void Debugger::eval_breakpoint(const std::vector<DebugBreakPoint *> &bps, std::vector<bool> &result,
                               uint64_t start, uint64_t end) {
    for (auto index = start; index < end; index++) {
        bool r = eval_breakpoint(bps[index]);
        result[index] = r;
    }
}

void Debugger::add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp) {
    scheduler_->add_breakpoint(bp_info, db_bp);
    process_delayed_breakpoint(db_bp.id);
}

void Debugger::start_breakpoint_evaluation() {
    scheduler_->start_breakpoint_evaluation();
    cached_signal_values_.clear();
    update_delayed_values();
}

void Debugger::add_cb_clocks() {
    // TODO:
    //     for now we assume all the designs share the same clock
    //     this may not be the case
    if (!namespaces_.empty() && namespaces_.default_rtl() &&
        !namespaces_.default_rtl()->is_verilator()) {
        // only trigger eval at the posedge clk
        auto *rtl = namespaces_.default_rtl();
        auto clock_signals = util::get_clock_signals(rtl, db_.get());
        bool r = rtl->monitor_signals(clock_signals, eval_hgdb_on_clk, this);
        if (!r || clock_signals.empty()) log_error("Failed to register evaluation callback");
    }
}

void Debugger::setup_init_breakpoint_from_env() {
    uint64_t i = 0;
    while (true) {
        auto breakpoint_name = fmt::format(DEBUG_BREAKPOINT_ENV, i++);
        auto const bp = util::getenv(breakpoint_name);
        if (!bp) {
            break;
        }
        // need to parse the actual filename and conditions
        // don't think @ is a normal filename, so use it to delimit string
        auto tokens = util::get_tokens(*bp, "@");
        if (tokens.empty()) {
            std::cerr << "Invalid breakpoint expression " << breakpoint_name << std::endl;
            continue;
        }
        auto const &filename_lin_num = tokens[0];
        auto fn_ln = util::get_tokens(filename_lin_num, ":");
        if (fn_ln.size() != 2 && fn_ln.size() != 3) {
            std::cerr << "Invalid breakpoint expression " << breakpoint_name << std::endl;
            continue;
        }
        BreakPoint breakpoint;
        breakpoint.filename = fn_ln[0];
        auto ln = util::stoul(fn_ln[1]);
        if (!ln) {
            std::cerr << "Invalid breakpoint expression " << breakpoint_name << std::endl;
            continue;
        }
        breakpoint.line_num = *ln;
        if (fn_ln.size() == 3) {
            auto cn = util::stoul(fn_ln[2]);
            if (!cn) {
                std::cerr << "Invalid breakpoint expression " << breakpoint_name << std::endl;
                continue;
            }
            breakpoint.column_num = *cn;
        }
        if (tokens.size() > 1) {
            breakpoint.condition = tokens[1];
        }
        log::log(log::log_level::info,
                 fmt::format("Preloading breakpoint @ {0}:{1}:{2} with condition {3}",
                             breakpoint.filename, breakpoint.line_num, breakpoint.column_num,
                             breakpoint.condition));
        BreakPointRequest req(breakpoint, BreakPointRequest::action::add);
        // use max channel ID just in case. in
        handle_breakpoint(req, std::numeric_limits<uint64_t>::max());
    }
}

void Debugger::preload_db_from_env() {
    auto const db_name = util::getenv(DATABASE_FILENAME_ENV);
    if (!db_name) [[likely]] {
        return;
    }
    initialize_db(*db_name);
    // also need to load clock signals
    add_cb_clocks();
}

bool Debugger::set_expr_values(uint32_t ns_id, DebugExpression *expr, uint32_t instance_id) {
    auto const &symbol_handles = expr->get_resolved_symbol_handles();
    const static std::unordered_map<std::string, int64_t> error = {};
    for (auto const &[symbol_name, handle] : symbol_handles) {
        if (symbol_name == util::instance_var_name) [[unlikely]] {
            expr->set_value(symbol_name, instance_id);
            continue;
        } else if (symbol_name == util::time_var_name) [[unlikely]] {
            expr->set_value(symbol_name,
                            static_cast<int64_t>(namespaces_[ns_id]->rtl->get_simulation_time()));
            continue;
        }
        auto v = get_signal_value(ns_id, handle);
        if (!v) return false;
        expr->set_value(symbol_name, *v);
    }
    return true;
}

void Debugger::update_delayed_values() {
    // notice that we never delete them, which can be a future improvement
    if (delayed_variables_.empty()) return;
    for (auto const &ns : namespaces_) {
        auto values =
            ns->monitor->get_watched_values(MonitorRequest::MonitorType::delay_clock_edge);
        for (auto &iter : delayed_variables_) {
            auto &var_info = iter.second;
            auto pos = std::find_if(values.begin(), values.end(), [&var_info](auto const &iter) {
                return iter.first == var_info.watch_id;
            });
            if (pos != values.end()) {
                var_info.value = pos->second;
            }
        }
    }
}

void Debugger::process_delayed_breakpoint(uint32_t bp_id) {
    if (!db_) [[unlikely]]
        return;
    auto *bp = scheduler_->get_breakpoint(bp_id);
    if (!bp) return;
    auto context_vars = db_->get_context_delayed_variables(bp_id);
    auto instance_name = db_->get_instance_name_from_bp(bp_id);
    auto const &namespaces = namespaces_.get_namespaces(*instance_name);
    for (auto *ns : namespaces) {
        auto *rtl = ns->rtl.get();
        auto *monitor = ns->monitor.get();
        auto func = [this, bp]() { return eval_breakpoint(bp); };

        for (auto const &[ctx, v] : context_vars) {
            auto var_names = resolve_context_name(v.value, ctx.name, bp_id, rtl, db_.get());
            for (auto const &[front_var, rtl_name] : var_names) {
                // we really don't care about front end name
                // get current value as initialization. this allows the breakpoint next cycle
                // has the proper value before monitor logic kicks in
                auto value = rtl->get_value(rtl_name);
                auto watch_id = monitor->add_monitor_variable(rtl_name, ctx.depth, value);
                monitor->set_monitor_variable_condition(watch_id, func);
                DelayedVariable delayed_var;
                delayed_var.rtl_name = rtl_name;
                delayed_var.watch_id = watch_id;
                auto *handle = rtl->get_handle(rtl_name);
                if (handle) {
                    delayed_variables_.emplace(handle, delayed_var);
                }
            }
        }
    }
}

}  // namespace hgdb