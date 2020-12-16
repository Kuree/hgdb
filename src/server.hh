#ifndef HGDB_SERVER_HH
#define HGDB_SERVER_HH

#include <mutex>
#include <set>

#include "websocketpp/config/asio_no_tls.hpp"
#include "websocketpp/server.hpp"

namespace hgdb {

using WSServer = websocketpp::server<websocketpp::config::asio>;
using Connection = WSServer::connection_ptr;

// wrapper for thee websocket
class DebugServer {
public:
    explicit DebugServer(uint16_t port);
    void run();
    void stop();
    void send(const std::string &payload);
    void set_on_message(const std::function<void(const std::string &)> &callback);

private:
    WSServer server_;

    // active connections
    std::mutex connections_lock_;
    std::set<Connection> connections_;
};

}  // namespace hgdb

#endif  // HGDB_SERVER_HH
