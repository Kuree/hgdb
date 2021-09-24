#ifndef HGDB_SERVER_HH
#define HGDB_SERVER_HH

#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "websocketpp/config/asio_no_tls.hpp"
#include "websocketpp/server.hpp"

namespace hgdb {

using WSServer = websocketpp::server<websocketpp::config::asio>;
using Connection = WSServer::connection_ptr;

// wrapper for thee websocket
class DebugServer {
public:
    explicit DebugServer();
    explicit DebugServer(bool enable_logging);
    void run(uint16_t port);
    void stop();
    void send(const std::string &payload);
    void send(const std::string &payload, const std::string &topic);
    void send(const std::string &payload, uint64_t conn_id);
    void set_on_message(const std::function<void(const std::string &, uint64_t conn_id)> &callback);
    void set_on_call_client_disconnect(const std::function<void(void)> &func);
    void add_to_topic(const std::string &topic, uint64_t conn_id);
    void remove_from_topic(const std::string &topic, uint64_t conn_id);

private:
    using ConnectionPtr = websocketpp::connection<websocketpp::config::asio> *;
    WSServer server_;

    // active connections
    std::mutex connections_lock_;
    std::unordered_map<uint64_t, Connection> connections_;
    // reverted map for connection id
    std::unordered_map<ConnectionPtr, uint64_t> connection_id_map_;

    // used for topics
    uint64_t channel_count_ = 0;
    std::unordered_map<std::string, std::unordered_set<uint64_t>> topics_;

    uint64_t get_new_channel_id();

    // call back on a connection closed
    std::optional<std::function<void(void)>> on_all_client_disconnect_;
};

}  // namespace hgdb

#endif  // HGDB_SERVER_HH
