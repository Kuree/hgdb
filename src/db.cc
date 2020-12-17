#include "db.hh"

namespace hgdb {

DebugDatabaseClient::DebugDatabaseClient(const std::string &filename) {
    db_ = std::make_unique<DebugDatabase>(init_debug_db(filename));
    db_->sync_schema();
}

void DebugDatabaseClient::close() {
    if (!is_closed_) {
        db_.reset();
        is_closed_ = true;
    }
}

DebugDatabaseClient::~DebugDatabaseClient() { close(); }

}  // namespace hgdb