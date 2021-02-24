#ifndef HGDB_SCHEDULER_HH
#define HGDB_SCHEDULER_HH

#include <mutex>

#include "db.hh"
#include "eval.hh"
#include "rtl.hh"

namespace hgdb {

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

class Scheduler {
public:
    Scheduler(RTLSimulatorClient *rtl, DebugDatabaseClient *db, const bool &single_thread_mode,
              const bool &log_enabled);
    enum class EvaluationMode { BreakPointOnly, StepOver, StepBack, ReverseBreakpointOnly, None };
    std::vector<DebugBreakPoint *> next_breakpoints();
    DebugBreakPoint *next_step_over_breakpoint();
    std::vector<DebugBreakPoint *> next_normal_breakpoints();
    DebugBreakPoint *next_step_back_breakpoint();
    std::vector<DebugBreakPoint *> next_reverse_breakpoints();
    void start_breakpoint_evaluation();

    // change scheduling semantics
    void set_evaluation_mode(EvaluationMode mode);
    void clear();

    // handle breakpoints
    void add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp);
    void reorder_breakpoints();
    void remove_breakpoint(const BreakPoint &bp);
    // getter. not exposing all the information
    std::vector<BreakPoint> get_current_breakpoints();

    // breakpoint mode
    bool breakpoint_only() const;

private:
    std::unordered_set<uint32_t> evaluated_ids_;
    std::optional<uint32_t> current_breakpoint_id_;

    EvaluationMode evaluation_mode_ = EvaluationMode::BreakPointOnly;

    std::vector<std::unique_ptr<DebugBreakPoint>> breakpoints_;
    std::unordered_set<uint32_t> inserted_breakpoints_;
    // look up table for ordering of breakpoints
    std::unordered_map<uint32_t, uint64_t> bp_ordering_table_;
    // need to ensure there is no concurrent modification
    std::mutex breakpoint_lock_;
    // holder for step over breakpoint, not used for normal purpose
    DebugBreakPoint next_temp_breakpoint_;

    // get it from the debugger. no ownership
    RTLSimulatorClient *rtl_;
    DebugDatabaseClient *db_;

    // some settings are directly shared from the debugger
    const bool &single_thread_mode_;
    const bool &log_enabled_;

    // cache clock handles as well
    std::vector<vpiHandle> clock_handles_;

    DebugBreakPoint *create_next_breakpoint(const std::optional<BreakPoint> &bp_info);

    // log
    static void log_error(const std::string &msg);
    void log_info(const std::string &msg) const;

    // scanning nearby breakpoints for multi-thread mode
    void scan_breakpoints(uint64_t ref_index, bool forward, std::vector<DebugBreakPoint *> &result);
};

namespace util {
constexpr auto time_var_name = "$time";
constexpr auto instance_var_name = "$instance";

void validate_expr(RTLSimulatorClient *rtl, DebugDatabaseClient *db, DebugExpression *expr,
                   std::optional<uint32_t> breakpoint_id, std::optional<uint32_t> instance_id);
std::vector<std::string> get_clock_signals(RTLSimulatorClient *rtl, DebugDatabaseClient *db);

}  // namespace util

}  // namespace hgdb

#endif  // HGDB_SCHEDULER_HH
