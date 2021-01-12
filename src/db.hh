#ifndef HGDB_DB_HH
#define HGDB_DB_HH

#include <mutex>
#include <unordered_map>
#include <vector>

#include "schema.hh"

namespace hgdb {
/**
 * Debug database class that handles querying about the design
 */
class DebugDatabaseClient {
public:
    explicit DebugDatabaseClient(const std::string &filename);
    // take over the DB ownership. normally used for testing
    explicit DebugDatabaseClient(std::unique_ptr<DebugDatabase> db);
    void close();

    // helper functions to query the database
    std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                            uint32_t col_num = 0);
    std::vector<BreakPoint> get_breakpoints(const std::string &filename);
    std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id);
    std::optional<std::string> get_instance_name(uint32_t breakpoint_id);
    using ContextVariableInfo = std::pair<ContextVariable, Variable>;
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(uint32_t breakpoint_id);
    using GeneratorVariableInfo = std::pair<GeneratorVariable, Variable>;
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(uint32_t instance_id);
    [[nodiscard]] std::vector<std::string> get_instance_names();
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name);

    ~DebugDatabaseClient();

    // accessors
    [[nodiscard]] const std::vector<uint32_t> &execution_bp_orders() const {
        return execution_bp_orders_;
    }

private:
    std::unique_ptr<DebugDatabase> db_;
    bool is_closed_ = false;
    std::mutex db_lock_;

    // we compute the execution order as we initialize the client, which is defined by the scope
    std::vector<uint32_t> execution_bp_orders_;

    void setup_execution_order();
    // scope table not provided - build from heuristics
    void build_execution_order_from_bp();
};
}  // namespace hgdb

#endif  // HGDB_DB_HH
