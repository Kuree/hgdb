#ifndef HGDB_DEBUG_HH
#define HGDB_DEBUG_HH
#include "db.hh"
#include "eval.hh"
#include "monitor.hh"
#include "proto.hh"
#include "rtl.hh"
#include "server.hh"
#include "thread.hh"

namespace hgdb {

namespace util {
class Options;
}

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
    [[maybe_unused]] [[nodiscard]] bool is_verilator();

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
        std::string filename;
        uint32_t line_num;
        uint32_t column_num;
        // this is to match with the always_comb semantics
        // first table stores the overall symbols that triggers
        // second table stores the seen value
        std::vector<std::string> trigger_symbols;
        std::unordered_map<std::string, int64_t> trigger_values;
    };
    std::vector<DebugBreakPoint> breakpoints_;
    std::unordered_set<uint32_t> inserted_breakpoints_;
    // look up table for ordering of breakpoints
    std::unordered_map<uint32_t, uint64_t> bp_ordering_table_;
    // need to ensure there is no concurrent modification
    std::mutex breakpoint_lock_;
    // holder for step over breakpoint, not used for normal purpose
    DebugBreakPoint next_temp_breakpoint_;

    // used for scheduler
    // if in single thread mode, instances with the same fn/ln won't be evaluated as a batch
    bool single_thread_mode_ = false;
    enum class EvaluationMode { BreakPointOnly, StepOver, StepBack, None };
    EvaluationMode evaluation_mode_ = EvaluationMode::BreakPointOnly;
    std::unordered_set<uint32_t> evaluated_ids_;
    std::optional<uint32_t> current_breakpoint_id_;
    // reduce VPI traffic
    std::unordered_map<std::string, int64_t> cached_signal_values_;
    std::mutex cached_signal_values_lock_;
    // reduce DB traffic and remapping computation
    std::unordered_map<uint64_t, std::string> cached_instance_name_;
    std::mutex cached_instance_name_lock_;

    // monitor logic
    Monitor monitor_;

    // handle client disconnect logic
    bool detach_after_disconnect_ = false;
    void detach();

    // message handler
    void on_message(const std::string &message, uint64_t conn_id);
    void send_message(const std::string &message);
    void send_message(const std::string &message, uint64_t conn_id);

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
    [[nodiscard]] static std::string get_monitor_topic(uint64_t watch_id);

    // request handler
    void handle_connection(const ConnectionRequest &req, uint64_t conn_id);
    void handle_breakpoint(const BreakPointRequest &req, uint64_t conn_id);
    void handle_breakpoint_id(const BreakPointIDRequest &req, uint64_t conn_id);
    void handle_bp_location(const BreakPointLocationRequest &req, uint64_t conn_id);
    void handle_command(const CommandRequest &req, uint64_t conn_id);
    void handle_debug_info(const DebuggerInformationRequest &req, uint64_t conn_id);
    void handle_path_mapping(const PathMappingRequest &req, uint64_t conn_id);
    void handle_evaluation(const EvaluationRequest &req, uint64_t conn_id);
    void handle_option_change(const OptionChangeRequest &req, uint64_t conn_id);
    void handle_monitor(const MonitorRequest &req, uint64_t conn_id);
    void handle_error(const ErrorRequest &req, uint64_t conn_id);

    // send functions
    void send_breakpoint_hit(const std::vector<const DebugBreakPoint *> &bps);
    void send_monitor_values(bool has_breakpoint);

    // options
    [[nodiscard]] util::Options get_options();
    // used to set initial options
    void set_vendor_initial_options();

    // common checker
    bool check_send_db_error(RequestType type, uint64_t conn_id);

    // scheduler
    std::vector<Debugger::DebugBreakPoint *> next_breakpoints();
    Debugger::DebugBreakPoint *next_step_over_breakpoint();
    std::vector<Debugger::DebugBreakPoint *> next_normal_breakpoints();
    Debugger::DebugBreakPoint *next_step_back_breakpoint();
    void start_breakpoint_evaluation();
    bool should_trigger(DebugBreakPoint *bp);
    void eval_breakpoint(DebugBreakPoint *bp, std::vector<bool> &result, uint32_t index);

    // cached wrapper
    std::optional<int64_t> get_value(const std::string &signal_name);
    std::string get_full_name(uint64_t instance_id, const std::string &var_name);

    // validate the expression
    void validate_expr(DebugExpression *expr, std::optional<uint32_t> breakpoint_id,
                       std::optional<uint32_t> instance_id);
    DebugBreakPoint *create_next_breakpoint(const std::optional<BreakPoint> &bp_info);
};

}  // namespace hgdb

#endif  // HGDB_DEBUG_HH
