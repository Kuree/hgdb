#ifndef HGDB_DB_HH
#define HGDB_DB_HH

#include "schema.hh"
#include <unordered_map>
#include <vector>

namespace hgdb {
/**
 * Debug database class that handles querying about the design
 */
class DebugDatabaseClient {
public:
    explicit DebugDatabaseClient(const std::string &filename);
    void close();

    // helper functions to query the database
    std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                            uint32_t col_num = 0);
    ~DebugDatabaseClient();

private:
    std::unique_ptr<DebugDatabase> db_;
    bool is_closed_ = false;

    // we compute the execution order as we initialize the client, which is defined by the scope
    // notice that map is ordered by design
    std::map<uint32_t, std::vector<uint32_t>> execution_bp_orders_;
    std::unordered_map<uint32_t, uint32_t> next_execution_bp_;

    void setup_execution_order();
    // scope table not provided - build from heuristics
    void build_execution_order_from_bp();

};
}  // namespace hgdb

#endif  // HGDB_DB_HH
