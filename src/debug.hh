#ifndef HGDB_DEBUG_HH
#define HGDB_DEBUG_HH
#include "db.hh"
#include "proto.hh"
#include "rtl.hh"
#include "server.hh"
#include "thread.hh"

namespace hgdb {

class Debugger {
public:
    Debugger();
    explicit Debugger(std::unique_ptr<AVPIProvider> vpi);

    void initialize_db(const std::string &filename);
    void initialize_db(std::unique_ptr<DebugDatabaseClient> db);
    void run();
    void stop();

    static constexpr uint16_t default_port_num = 8888;
    static constexpr bool default_logging = false;

    // status to expose to outside world
    [[nodiscard]] const std::atomic<bool> & is_running() const { return is_running_; }

private:
    std::unique_ptr<RTLSimulatorClient> rtl_;
    std::unique_ptr<DebugDatabaseClient> db_;
    std::unique_ptr<DebugServer> server_;
    // logging
    bool log_enabled_ = default_logging;

    // server thread
    std::thread server_thread_;
    // runtime lock
    RuntimeLock lock_;
    std::atomic<bool> is_running_ = false;

    // message handler
    void on_message(const std::string &message);

    // helper functions
    uint16_t get_port();
    bool get_logging();
    void log_error(const std::string &msg) const;
    void log_info(const std::string &msg) const;

    // request handler
    void handle_connection(const ConnectionRequest &req);
    void handle_breakpoint(const BreakpointRequest &req);
    void handle_bp_location(const BreakPointLocationRequest &req);
    void handle_command(const CommandRequest &req);
    void handle_error(const ErrorRequest &req);
};

}  // namespace hgdb

#endif  // HGDB_DEBUG_HH
