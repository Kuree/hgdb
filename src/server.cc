#include "server.hh"

namespace hgdb {

using raw_message = WSServer::message_ptr;

DebugServer::DebugServer() : DebugServer(false) {}

DebugServer::DebugServer(bool enable_logging) {
    using websocketpp::lib::bind;
    // set logging settings
    if (enable_logging) {
        server_.set_access_channels(websocketpp::log::alevel::all);
        server_.clear_access_channels(websocketpp::log::alevel::frame_payload);
    } else {
        server_.clear_access_channels(websocketpp::log::alevel::all);
    }

    // on connection
    auto on_connect = [this](websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> guard{connections_lock_};
        auto conn = server_.get_con_from_hdl(std::move(hdl));
        auto id = get_new_channel_id();
        connections_.emplace(id, conn);
        connection_id_map_.emplace(conn.get(), id);
    };
    // on disconnection
    auto on_disconnect = [this](websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> guard{connections_lock_};
        auto conn = server_.get_con_from_hdl(std::move(hdl));
        // not efficient, but we assume client won't disconnect often
        for (auto const &[id, c] : connections_) {
            if (conn == c) {
                connections_.erase(id);
                connection_id_map_.erase(conn.get());
                break;
            }
        }
    };

    // bind some methods to keep track of connections
    server_.set_open_handler(on_connect);
    server_.set_close_handler(on_disconnect);

    // initialize Asio
    server_.init_asio();
}

void DebugServer::run(uint16_t port) {
    server_.listen(port);
    server_.start_accept();
    server_.run();
}

void DebugServer::stop() {
    server_.stop_listening();
    // close all the ongoing connections
    {
        std::lock_guard guard(connections_lock_);
        for (auto const &[id, conn] : connections_) {
            conn->close(websocketpp::close::status::going_away, "Server stopped upon user request");
        }
        connections_.clear();
        connection_id_map_.clear();
    }
    server_.stop();
}

void DebugServer::send(const std::string &payload) {
    for (const auto &[id, conn] : connections_) {
        conn->send(payload);
    }
}

void DebugServer::send(const std::string &payload, const std::string &topic) {
    if (topics_.find(topic) != topics_.end()) [[likely]] {
        auto const &ids = topics_.at(topic);
        // to ensure high performance during runtime, we don't do clean up
        // even through the channel is closed, which we assume happens infrequently
        for (auto const id : ids) {
            if (connections_.find(id) != connections_.end()) [[likely]] {
                connections_.at(id)->send(payload);
            }
        }
    }
}

void DebugServer::send(const std::string &payload, uint64_t conn_id) {
    if (connections_.find(conn_id) != connections_.end()) [[likely]] {
        connections_.at(conn_id)->send(payload);
    }
}

void DebugServer::set_on_message(
    const std::function<void(const std::string &, uint64_t)> &callback) {
    auto on_message = [this, callback](const websocketpp::connection_hdl &hdl,
                                       const raw_message &msg) {
        auto str = msg->get_payload();
        auto conn = server_.get_con_from_hdl(hdl);
        auto id = connection_id_map_.at(conn.get());
        callback(str, id);
    };
    server_.set_message_handler(on_message);
}

void DebugServer::add_to_topic(const std::string &topic, uint64_t conn_id) {
    topics_[topic].emplace(conn_id);
}

uint64_t DebugServer::get_new_channel_id() {
    // assume we are under lock guard's protection
    return channel_count_++;
}

}  // namespace hgdb
