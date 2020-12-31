#ifndef HGDB_DEBUG_HH
#define HGDB_DEBUG_HH
#include "db.hh"
#include "proto.hh"
#include "rtl.hh"
#include "server.hh"
#include "eval.hh"
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
    void eval();

    // default and hardcoded values
    static constexpr uint16_t default_port_num = 8888;
    static constexpr bool default_logging = false;
    static constexpr auto error_value_str = "ERROR";

    // status to expose to outside world
    [[nodiscard]] const std::atomic<bool> & is_running() const { return is_running_; }

    ~Debugger();

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

    // breakpoint information
    struct DebugBreakPoint {
        uint32_t id;
        uint32_t instance_id;
        std::unique_ptr<DebugExpression> expr;
        std::unique_ptr<DebugExpression> enable_expr;
    };
    std::vector<DebugBreakPoint> breakpoints_;
    std::unordered_set<uint32_t> inserted_breakpoints_;
    // look up table for ordering of breakpoints
    std::unordered_map<uint32_t, uint64_t> bp_ordering_table_;
    // need to ensure there is no concurrent modification
    std::mutex breakpoint_lock_;

    // used for scheduler
    enum class EvaluationMode {
        BreakPointOnly,
        StepOver
    };
    EvaluationMode evaluation_mode_ = EvaluationMode::BreakPointOnly;
    std::unordered_set<uint32_t> evaluated_ids_;
    std::optional<uint32_t> current_breakpoint_id_;

    // message handler
    void on_message(const std::string &message);
    void send_message(const std::string &message);

    // helper functions
    uint16_t get_port();
    bool get_logging();
    static void log_error(const std::string &msg) ;
    void log_info(const std::string &msg) const;
    void initialize_rtl(const std::unordered_set<std::string> &instance_names);

    // request handler
    void handle_connection(const ConnectionRequest &req);
    void handle_breakpoint(const BreakPointRequest &req);
    void handle_bp_location(const BreakPointLocationRequest &req);
    void handle_command(const CommandRequest &req);
    void handle_debug_info(const DebuggerInformationRequest &req);
    void handle_error(const ErrorRequest &req);

    // send functions
    void send_breakpoint_hit(const DebugBreakPoint &bp);

    // common checker
    bool check_send_db_error(RequestType type);

    // scheduler
    Debugger::DebugBreakPoint* next_breakpoint();
    void start_breakpoint_evaluation();
};

}  // namespace hgdb

#endif  // HGDB_DEBUG_HH
