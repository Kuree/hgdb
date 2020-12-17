#ifndef HGDB_DB_HH
#define HGDB_DB_HH

#include "schema.hh"

namespace hgdb {
/**
 * Debug database class that handles querying about the design
 */
class DebugDatabaseClient {
public:
    explicit DebugDatabaseClient(const std::string &filename);
    void close();

    ~DebugDatabaseClient();

private:
    std::unique_ptr<DebugDatabase> db_;
    bool is_closed_ = false;
};
}  // namespace hgdb

#endif  // HGDB_DB_HH
