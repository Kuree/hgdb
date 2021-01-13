#ifndef HGDB_DEBUG_HH
#define HGDB_DEBUG_HH
#include "db.hh"
#include "eval.hh"
#include "proto.hh"
#include "rtl.hh"
#include "server.hh"
#include "thread.hh"

namespace hgdb {

class Debugger {
public:
    Debugger();
    explicit Debugger(std::unique_ptr<AVPIProvider> vpi);

    bool initialize_db(const std::string &filename);
    void initialize_db(std::unique_ptr<DebugDatabaseClient> db);
    void run();
    void stop();
    void eval();

    // some public information about the debugger
    [[nodiscard]] bool is_verilator();

    // default and hardcoded values
    static constexpr uint16_t default_port_num = 8888;
    static constexpr bool default_logging = false;
    static constexpr auto error_value_str = "ERROR";
    static constexpr auto debug_skip_db_load = "+DEBUG_NO_DB";

    // status to expose to outside world
    [[nodiscard]] const std::atomic<bool> &is_running() const { return is_running_; }
    // outside world can directly control the RTL client if necessary
    [[nodiscard]] RTLSimulatorClient *rtl_client() const { return rtl_.get(); }

    // used to monitor clocks
    [[nodiscard]] std::vector<std::string> get_clock_signals();

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
    // holder for step over breakpoint, not used for normal purpose
    DebugBreakPoint step_over_breakpoint_;

    // used for scheduler
    enum class EvaluationMode { BreakPointOnly, StepOver };
    EvaluationMode evaluation_mode_ = EvaluationMode::BreakPointOnly;
    std::unordered_set<uint32_t> evaluated_ids_;
    std::optional<uint32_t> current_breakpoint_id_;

    // message handler
    void on_message(const std::string &message);
    void send_message(const std::string &message);

    // helper functions
    uint16_t get_port();
    bool get_logging();
    static void log_error(const std::string &msg);
    void log_info(const std::string &msg) const;
    std::unordered_map<std::string, int64_t> get_context_static_values(uint32_t breakpoint_id);
    void add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp);
    void reorder_breakpoints();
    void remove_breakpoint(const BreakPoint &bp);
    bool has_cli_flag(const std::string &flag);

    // request handler
    void handle_connection(const ConnectionRequest &req);
    void handle_breakpoint(const BreakPointRequest &req);
    void handle_breakpoint_id(const BreakPointIDRequest &req);
    void handle_bp_location(const BreakPointLocationRequest &req);
    void handle_command(const CommandRequest &req);
    void handle_debug_info(const DebuggerInformationRequest &req);
    void handle_error(const ErrorRequest &req);

    // send functions
    void send_breakpoint_hit(const DebugBreakPoint &bp);

    // common checker
    bool check_send_db_error(RequestType type);

    // scheduler
    Debugger::DebugBreakPoint *next_breakpoint();
    void start_breakpoint_evaluation();
};

}  // namespace hgdb

#endif  // HGDB_DEBUG_HH
