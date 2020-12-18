#ifndef HGDB_DB_HH
#define HGDB_DB_HH

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
    explicit DebugDatabaseClient(std::unique_ptr<DebugDatabase> &db);
    void close();

    // helper functions to query the database
    std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                            uint32_t col_num = 0);
    ~DebugDatabaseClient();

    // accessors
    const std::vector<uint32_t> &execution_bp_orders() const { return execution_bp_orders_; }

private:
    std::unique_ptr<DebugDatabase> db_;
    bool is_closed_ = false;

    // we compute the execution order as we initialize the client, which is defined by the scope
    std::vector<uint32_t> execution_bp_orders_;

    void setup_execution_order();
    // scope table not provided - build from heuristics
    void build_execution_order_from_bp();
};
}  // namespace hgdb

#endif  // HGDB_DB_HH
