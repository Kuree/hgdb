#include "debug.hh"

#include <filesystem>
#include <functional>

#include "fmt/format.h"
#include "log.hh"

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

void Debugger::initialize_db(const std::string &filename) {
    // we cannot accept in-memory database since in the debug mode,
    // it is readonly
    if (!fs::exists(filename)) {
        log_error(fmt::format("{0} does not exist", filename));
        return;
    }
    initialize_db(std::make_unique<DebugDatabaseClient>(filename));
}

void Debugger::initialize_db(std::unique_ptr<DebugDatabaseClient> db) {
    if (!db) return;
    db_ = std::move(db);
    // compute the look up table
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
    server_->stop();
    is_running_ = false;
}

void Debugger::eval() {
    // the function that actually triggers breakpoints!
    // notice that there is a hidden race condition
    // when we trigger the breakpoint, the runtime (simulation side) will be paused via a lock.
    // however, the server side can still takes breakpoint requests, hence modifying the
    // breakpoints_.
    start_breakpoint_evaluation();  // clean the state
    while (true) {
        auto bp = next_breakpoint();
        if (!bp) break;
        auto &bp_expr = bp->expr;
        // get table values
        auto const &symbols = bp_expr.symbols();
        auto const bp_id = bp->id;
        auto const instance_name_ = db_->get_instance_name(bp_id);
        if (!instance_name_) continue;
        auto const &instance_name = *instance_name_;
        std::unordered_map<std::string, int64_t> values;
        // need to query through all its symbols
        for (auto const &symbol_name : symbols) {
            // need to map these symbol names into the actual hierarchy
            // name
            auto name = fmt::format("{0}.{1}", instance_name, symbol_name);
            auto v = rtl_->get_value(name);
            if (!v) break;
            values.emplace(symbol_name, *v);
        }
        if (values.size() != symbols.size()) {
            // something went wrong with the querying symbol
            log_error(fmt::format("Unable to evaluate breakpoint %d", bp_id));
        } else {
            auto result = bp_expr.eval(values);
            if (result) {
                // trigger a breakpoint!
                send_breakpoint_hit(bp->id);
                // then pause the execution
                lock_.wait();
            }
        }
    }
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
        default: {
            // do nothing
            return;
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

void Debugger::handle_connection(const ConnectionRequest &req) {
    auto const &db_filename = req.db_filename();
    // path mapping not supported yet
    initialize_db(db_filename);
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
            req.set_token(error_response);
            send_message(error_response.str(log_enabled_));
            return;
        }

        {
            std::lock_guard guard(breakpoint_lock_);
            breakpoints_.reserve(breakpoints_.size() + bps.size());
            for (auto const &bp : bps) {
                // add them to the eval vector
                std::string cond = "1";
                if (!bp.condition.empty()) cond.append(" and " + bp.condition);
                if (bp_info.condition.empty()) cond.append(" and " + bp_info.condition);
                if (inserted_breakpoints_.find(bp.id) == inserted_breakpoints_.end()) {
                    breakpoints_.emplace_back(
                        DebugBreakPoint{.id = bp.id, .expr = DebugExpression(cond)});
                    inserted_breakpoints_.emplace(bp.id);
                } else {
                    // update breakpoint entry
                    for (auto &b : breakpoints_) {
                        if (bp.id == b.id) {
                            b.expr = DebugExpression(cond);
                            continue;
                        }
                    }
                }
            }
            // need to sort them by the ordering
            // the easiest way is to sort them by their lookup table. assuming the number of
            // breakpoints is relatively small, i.e. < 100, sorting can be efficient and less
            // bug-prone
            std::sort(breakpoints_.begin(), breakpoints_.end(),
                      [this](const auto &left, const auto &right) -> bool {
                          return bp_ordering_table_.at(left.id) < bp_ordering_table_.at(right.id);
                      });
        }

        // tell client we're good
        auto success_resp = GenericResponse(status_code::success, req);
        req.set_token(success_resp);
        send_message(success_resp.str(log_enabled_));
    } else {
        // remove
        auto bps = db_->get_breakpoints(bp_info.filename, bp_info.line_num, bp_info.column_num);

        {
            std::lock_guard guard(breakpoint_lock_);
            // notice that removal doesn't need reordering
            for (auto const &bp : bps) {
                for (auto pos = breakpoints_.begin(); pos != breakpoints_.end(); pos++) {
                    if (pos->id == bp.id) {
                        breakpoints_.erase(pos);
                        inserted_breakpoints_.erase(bp.id);
                        break;
                    }
                }
            }
        }

        auto success_resp = GenericResponse(status_code::success, req);
        req.set_token(success_resp);
        send_message(success_resp.str(log_enabled_));
    }
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
            lock_.ready();
            break;
        }
        case CommandRequest::CommandType::stop: {
            lock_.ready();
            rtl_->finish_sim();
            stop();
            break;
        }
        case CommandRequest::CommandType::step_through: {
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
            req.set_token(resp);
            send_message(resp.str(log_enabled_));
        }
    }
}

void Debugger::handle_error(const ErrorRequest &req) {}

void Debugger::send_breakpoint_hit(uint32_t bp_id) {
    // we send it here to avoid a round trip of client asking for context and send send it
    // back
    // first need to query all the values
    auto generator_values = db_->get_generator_variable(bp_id);
    auto context_values = db_->get_context_variables(bp_id);
    auto bp = db_->get_breakpoint(bp_id);
    BreakPointResponse resp(rtl_->get_simulation_time(), bp->filename, bp->line_num,
                            bp->column_num);
    using namespace std::string_literals;
    for (auto const &[gen_var, var] : generator_values) {
        std::string value_str;
        if (var.is_rtl) {
            auto value = rtl_->get_value(var.value);
            value_str = value ? std::to_string(*value) : error_value_str;
        } else {
            value_str = var.value;
        }
        auto var_name = gen_var.name;
        resp.add_generator_value(var_name, value_str);
    }

    for (auto const &[gen_var, var] : context_values) {
        std::string value_str;
        if (var.is_rtl) {
            auto value = rtl_->get_value(var.value);
            value_str = value ? std::to_string(*value) : error_value_str;
        } else {
            value_str = var.value;
        }
        auto var_name = gen_var.name;
        resp.add_local_value(var_name, value_str);
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

Debugger::DebugBreakPoint *Debugger::next_breakpoint() {
    // depends on which execution order we have
    if (evaluation_mode_ == EvaluationMode::BreakPointOnly) {
        // we need to make the experience the same as debugging software
        // as a result, when user add new breakpoints to the list that has high priority,
        // we need to skip then and evaluate them at the next evaluation cycle
        uint64_t index = 0;
        std::lock_guard guard(breakpoint_lock_);
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
                return nullptr;
            }
        }
        current_breakpoint_id_ = breakpoints_[index].id;
        return &breakpoints_[*current_breakpoint_id_];

    } else if (evaluation_mode_ == EvaluationMode::StepOver) {
        if (current_breakpoint_id_) {
            auto current_id = *current_breakpoint_id_;
            // need to get the actual ordering table
            auto const &orders = db_->execution_bp_orders();
            auto pos = std::find(orders.begin(), orders.end(), current_id);
            if (pos != orders.end()) {
                auto index = static_cast<uint64_t>(std::distance(orders.begin(), pos));
                if (index != (orders.size() - 1)) {
                    current_breakpoint_id_ = breakpoints_[index + 1].id;
                    return &breakpoints_[*current_breakpoint_id_];
                }
            }
            return nullptr;
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }
}

void Debugger::start_breakpoint_evaluation() {
    evaluated_ids_.clear();
    current_breakpoint_id_ = std::nullopt;
}

}  // namespace hgdb