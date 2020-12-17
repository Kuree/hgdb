#include "db.hh"

#include "util.hh"

namespace hgdb {

DebugDatabaseClient::DebugDatabaseClient(const std::string &filename) {
    db_ = std::make_unique<DebugDatabase>(init_debug_db(filename));
    db_->sync_schema();

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
        std::vector<uint32_t> entries;
        entries.reserve(id_tokens.size());
        for (uint32_t i = 0; i < id_tokens.size(); i++) {
            auto const &s_id = id_tokens[i];
            auto id = std::stoul(s_id);
            entries.emplace_back(id);
            // link to the next
            if (i > 0) {
                auto pre_id = entries[i - 1];
                next_execution_bp_.emplace(pre_id, id);
            }
        }
        execution_bp_orders_.emplace(scope.id, entries);
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
        auto scope_id = execution_bp_orders_.size();
        execution_bp_orders_[scope_id].reserve(iter.second.size());
        for (auto const &iter_ : iter.second) {
            execution_bp_orders_[scope_id].emplace_back(iter_.second);
        }
    }
    // compute the next execution order
    for (auto const &iter : execution_bp_orders_) {
        auto const &bps_ = iter.second;
        for (uint64_t i = 1; i < bps_.size(); i++) {
            next_execution_bp_.emplace(bps_[i - 1], bps_[i]);
        }
    }
}

}  // namespace hgdb