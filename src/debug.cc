#include "debug.hh"

#include <filesystem>

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

Debugger::~Debugger() { server_thread_.join(); }

void Debugger::on_message(const std::string &message) {
    // server can only receives request
    auto req = Request::parse_request(message);
    if (req->status() != status_code::success) return;
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
    if (!check_send_db_error()) return;

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

        }
        breakpoints_.reserve(breakpoints_.size() + bps.size());
        for (auto const &bp : bps) {
            // add them to the eval vector
            std::string cond = "1";
            if (!bp.condition.empty()) cond.append(" and " + bp.condition);
            if (bp_info.condition.empty()) cond.append(" and " + bp_info.condition);
            breakpoints_.emplace_back(DebugBreakPoint{.id = bp.id, .expr = DebugExpression(cond)});
        }
        // need to sort them by the ordering
        // the easiest way is to sort them by their lookup table. assuming the number of breakpoints
        // is relatively small, i.e. < 100, sorting can be efficient and less bug-prone
        std::sort(breakpoints_.begin(), breakpoints_.end(),
                  [this](const auto &left, const auto &right) -> bool {
                      return bp_ordering_table_.at(left.id) < bp_ordering_table_.at(right.id);
                  });
    } else {
        // remove
        auto bps = db_->get_breakpoints(bp_info.filename, bp_info.line_num, bp_info.column_num);
        // notice that removal doesn't need reordering
        for (auto const &bp : bps) {
            for (auto pos = breakpoints_.begin(); pos != breakpoints_.end(); pos++) {
                if (pos->id == bp.id) {
                    breakpoints_.erase(pos);
                    break;
                }
            }
        }
    }
}

void Debugger::handle_bp_location(const BreakPointLocationRequest &req) {
    // if db is not connected correctly, abort
    if (!check_send_db_error()) return;

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

void Debugger::handle_error(const ErrorRequest &req) {}

bool Debugger::check_send_db_error() {
    if (!db_) {
        // need to send error response
        auto resp = GenericResponse(status_code::error,
                                    "Database is not initialized or is initialized incorrectly");
        send_message(resp.str(log_enabled_));
        return false;
    }
    return true;
}

}  // namespace hgdb