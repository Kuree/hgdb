#ifndef HGDB_SCHEDULER_HH
#define HGDB_SCHEDULER_HH

#include <mutex>

#include "eval.hh"
#include "rtl.hh"
#include "symbol.hh"

namespace hgdb {

struct DebugBreakPoint {
    enum class Type { normal = 1 << 0, data = 1 << 1 };
    uint32_t id = 0;
    uint32_t instance_id = 0;
    std::unique_ptr<DebugExpression> expr;
    std::unique_ptr<DebugExpression> enable_expr;
    std::string filename;
    uint32_t line_num = 0;
    uint32_t column_num = 0;
    // this is to match with the always_comb semantics
    // first table stores the overall symbols that triggers
    // second table stores the seen value
    std::vector<std::string> trigger_symbols;
    std::unordered_map<std::string, int64_t> trigger_values;

    // used for data breakpoint
    Type type = Type::normal;
    std::string full_rtl_var_name;
    uint64_t watch_id = 0;

    [[nodiscard]] inline bool has_type_flag(Type type_) const {
        return static_cast<uint64_t>(type) & static_cast<uint64_t>(type_);
    }
};

class Scheduler {
public:
    Scheduler(RTLSimulatorClient *rtl, SymbolTableProvider *db, const bool &single_thread_mode,
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
    DebugBreakPoint *add_breakpoint(const BreakPoint &bp_info, const BreakPoint &db_bp,
                                    DebugBreakPoint::Type bp_type = DebugBreakPoint::Type::normal,
                                    bool data_breakpoint = false,
                                    const std::string &target_var = "");
    void reorder_breakpoints();
    void remove_breakpoint(const BreakPoint &bp);
    std::vector<const DebugBreakPoint *> get_current_breakpoints();
    DebugBreakPoint *add_data_breakpoint(const std::string &var_name, const std::string &expression,
                                         const BreakPoint &db_bp);
    void clear_data_breakpoints();
    void remove_data_breakpoint(uint64_t bp_id);

    // breakpoint mode
    bool breakpoint_only() const;

    [[nodiscard]] const std::vector<vpiHandle> &clock_handles() const { return clock_handles_; }

private:
    std::unordered_set<uint32_t> evaluated_ids_;
    std::optional<uint32_t> current_breakpoint_id_;

    EvaluationMode evaluation_mode_ = EvaluationMode::BreakPointOnly;

    std::vector<std::unique_ptr<DebugBreakPoint>> breakpoints_;
    std::unordered_set<uint32_t> inserted_breakpoints_;
    // look up table for ordering of breakpoints
    std::unordered_map<uint32_t, uint64_t> bp_ordering_table_;
    std::vector<uint32_t> bp_ordering_;
    // need to ensure there is no concurrent modification
    std::mutex breakpoint_lock_;
    // holder for step over breakpoint, not used for normal purpose
    DebugBreakPoint next_temp_breakpoint_;

    // get it from the debugger. no ownership
    RTLSimulatorClient *rtl_;
    SymbolTableProvider *db_;

    // some settings are directly shared from the debugger
    const bool &single_thread_mode_;
    const bool &log_enabled_;

    // cache clock handles as well
    std::vector<vpiHandle> clock_handles_;

    DebugBreakPoint *create_next_breakpoint(const std::optional<BreakPoint> &bp_info);
    void remove_breakpoint(uint64_t bp_id);

    // log
    static void log_error(const std::string &msg);
    void log_info(const std::string &msg) const;

    // scanning nearby breakpoints for multi-thread mode
    void scan_breakpoints(uint64_t ref_index, bool forward, std::vector<DebugBreakPoint *> &result);
};

namespace util {
constexpr auto time_var_name = "$time";
constexpr auto instance_var_name = "$instance";

void validate_expr(RTLSimulatorClient *rtl, SymbolTableProvider *db, DebugExpression *expr,
                   std::optional<uint32_t> breakpoint_id, std::optional<uint32_t> instance_id);
std::vector<std::string> get_clock_signals(RTLSimulatorClient *rtl, SymbolTableProvider *db);

}  // namespace util

}  // namespace hgdb

#endif  // HGDB_SCHEDULER_HH
