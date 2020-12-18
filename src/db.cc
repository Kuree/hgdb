#include "db.hh"

#include "util.hh"

namespace hgdb {

DebugDatabaseClient::DebugDatabaseClient(const std::string &filename) {
    db_ = std::make_unique<DebugDatabase>(init_debug_db(filename));
    db_->sync_schema();

    setup_execution_order();
}

DebugDatabaseClient::DebugDatabaseClient(std::unique_ptr<DebugDatabase> &db) {
    // this will transfer ownership
    db_ = std::move(db);
    setup_execution_order();
}

void DebugDatabaseClient::close() {
    if (!is_closed_) {
        db_.reset();
        is_closed_ = true;
    }
}

DebugDatabaseClient::~DebugDatabaseClient() { close(); }

void DebugDatabaseClient::setup_execution_order() {
    auto scopes = db_->get_all<Scope>();
    if (scopes.empty()) {
        build_execution_order_from_bp();
        return;
    }
    for (auto const &scope : scopes) {
        auto const &ids = scope.breakpoints;
        auto id_tokens = util::get_tokens(ids, " ");
        for (auto const &s_id : id_tokens) {
            auto id = std::stoul(s_id);
            execution_bp_orders_.emplace_back(id);
        }
    }
}

void DebugDatabaseClient::build_execution_order_from_bp() {
    // use map's ordered ability
    std::map<std::string, std::map<uint32_t, uint32_t>> bp_ids;
    auto bps = db_->get_all<BreakPoint>();
    for (auto const &bp : bps) {
        bp_ids[bp.filename].emplace(bp.line_num, bp.id);
    }
    for (auto const &iter : bp_ids) {
        execution_bp_orders_.reserve(execution_bp_orders_.size() + iter.second.size());
        for (auto const &iter_ : iter.second) {
            execution_bp_orders_.emplace_back(iter_.second);
        }
    }
}

}  // namespace hgdb