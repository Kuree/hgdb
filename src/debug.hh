#ifndef HGDB_DEBUG_HH
#define HGDB_DEBUG_HH
#include "eval.hh"
#include "monitor.hh"
#include "proto.hh"
#include "scheduler.hh"
#include "server.hh"
#include "thread.hh"

namespace hgdb {

namespace util {
class Options;
}

class DebuggerTestFriend;

class Debugger {
public:
    Debugger();
    explicit Debugger(std::unique_ptr<AVPIProvider> vpi);

    bool initialize_db(const std::string &filename);
    void initialize_db(std::unique_ptr<SymbolTableProvider> db);
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
    [[nodiscard]] SymbolTableProvider *db() const { return db_.get(); }
    [[nodiscard]] Scheduler *scheduler() const { return scheduler_.get(); }

    // directly set options from function API instead of through ws
    void set_option(const std::string &name, bool value);

    // set callbacks
    void set_on_client_connected(const std::function<void(hgdb::SymbolTableProvider &)> &func);

    ~Debugger();

private:
    std::unique_ptr<RTLSimulatorClient> rtl_;
    std::unique_ptr<SymbolTableProvider> db_;
    std::unique_ptr<DebugServer> server_;
    // logging
    bool log_enabled_ = default_logging;

    // server thread
    std::thread server_thread_;
    // runtime lock
    RuntimeLock lock_;
    std::atomic<bool> is_running_ = false;

    // used for scheduler
    std::unique_ptr<Scheduler> scheduler_;

    // reduce VPI traffic
    std::unordered_map<vpiHandle, int64_t> cached_signal_values_;
    std::mutex cached_signal_values_lock_;
    // reduce DB traffic and remapping computation
    std::unordered_map<uint64_t, std::string> cached_instance_name_;
    std::mutex cached_instance_name_lock_;

    // monitor logic
    Monitor monitor_;

    // delay logic for symbols. no lock since they don't run interactively
    struct DelayedVariable {
        std::string rtl_name;
        std::optional<int64_t> value;
        uint64_t watch_id;
    };
    std::unordered_map<vpiHandle, DelayedVariable> delayed_variables_;

    // options
    // if in single thread mode, instances with the same fn/ln won't be evaluated as a batch
    bool single_thread_mode_ = false;
    // handle client disconnect logic
    bool detach_after_disconnect_ = false;
    // whether to retrieve hex string instead of as integer
    bool use_hex_str_ = false;
    // whether to pause at clock edge
    bool pause_at_posedge = false;
    // whether to collect perf. only useful when PERF_COUNT is turned on in cmake
    bool perf_count_ = false;
    // whether to use signal value cache. by default, it is false
    bool use_signal_cache_ = false;

    void detach();

    // message handler
    void on_message(const std::string &message, uint64_t conn_id);
    void send_message(const std::string &message);
    void send_message(const std::string &message, uint64_t conn_id);

    // helper functions
    uint16_t get_port();
    std::optional<std::string> get_value_plus_arg(const std::string &arg_name,
                                                  bool check_eng = false);
    bool get_test_plus_arg(const std::string &arg_name, bool check_env = false);
    bool get_logging();
    bool get_perf_count();
    static void log_error(const std::string &msg);
    void log_info(const std::string &msg) const;
    bool has_cli_flag(const std::string &flag);
    [[nodiscard]] static std::string get_monitor_topic(uint64_t watch_id);
    std::string get_value_str(const std::string &rtl_name, bool is_rtl, bool use_delay = false);
    std::optional<int64_t> get_value(const std::string &expression, uint32_t instance_id);
    std::optional<std::string> resolve_var_name(const std::string &var_name,
                                                const std::optional<uint64_t> &instance_id,
                                                const std::optional<uint64_t> &breakpoint_id);

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
    void handle_set_value(const SetValueRequest &req, uint64_t conn_id);
    void handle_error(const ErrorRequest &req, uint64_t conn_id);
    void handle_symbol(const SymbolRequest &req, uint64_t conn_id);
    void handle_data_breakpoint(const DataBreakpointRequest &req, uint64_t conn_id);

    // send functions
    void send_breakpoint_hit(const std::vector<const DebugBreakPoint *> &bps);
    void send_monitor_values(MonitorRequest::MonitorType type);

    // options
    [[nodiscard]] util::Options get_options();
    // used to set initial options
    void set_vendor_initial_options();

    // common checker
    bool check_send_db_error(RequestType type, uint64_t conn_id);

    // scheduler
    bool should_trigger(DebugBreakPoint *bp);
    bool eval_breakpoint(DebugBreakPoint *bp);
    void eval_breakpoint(const std::vector<DebugBreakPoint *> &bps, std::vector<bool> &result,
                         uint64_t start, uint64_t end);
    std::vector<bool> eval_breakpoints(const std::vector<DebugBreakPoint *> &bps);
    void add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp);
    void start_breakpoint_evaluation();

    // cached wrapper
    std::optional<int64_t> get_signal_value(vpiHandle handle, bool use_delayed = false);
    std::string get_full_name(uint64_t instance_id, const std::string &var_name);

    // callbacks
    std::optional<std::function<void(hgdb::SymbolTableProvider &)>> on_client_connected_;
    void add_cb_clocks();

    // performance benchmark functions
    // only used to initialize the debugger to certain state, not for normal usage
    void setup_init_breakpoint_from_env();
    void preload_db_from_env();

    bool set_expr_values(DebugExpression *expr, uint32_t instance_id);

    // update delayed values
    void update_delayed_values();
    void process_delayed_breakpoint(uint32_t bp_id);

    // test related
public:
    friend class DebuggerTestFriend;
};

}  // namespace hgdb

#endif  // HGDB_DEBUG_HH
