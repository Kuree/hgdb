#include "server.hh"

namespace hgdb {

using raw_message = WSServer::message_ptr;

DebugServer::DebugServer(uint16_t port) {
    using websocketpp::lib::bind;
    // set logging settings
    server_.set_access_channels(websocketpp::log::alevel::all);
    server_.clear_access_channels(websocketpp::log::alevel::frame_payload);

    // on connection
    auto on_connect = [this](websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> guard{connections_lock_};
        auto conn = server_.get_con_from_hdl(std::move(hdl));
        connections_.emplace(conn);
    };
    // on disconnection
    auto on_disconnect = [this](websocketpp::connection_hdl hdl) {
      std::lock_guard<std::mutex> guard{connections_lock_};
        auto conn = server_.get_con_from_hdl(std::move(hdl));
        connections_.erase(conn);
    };

    // bind some methods to keep track of connections
    server_.set_open_handler(on_connect);
    server_.set_close_handler(on_disconnect);

    // initialize Asio
    server_.init_asio();

    server_.listen(port);
}

void DebugServer::run() {
    server_.start_accept();
    server_.run();
}

void DebugServer::stop() {
    server_.stop_listening();
    server_.stop();
}

void DebugServer::send(const std::string &payload) {
    for (auto &conn : connections_) {
        conn->send(payload);
    }
}

void DebugServer::set_on_message(const std::function<void(const std::string &)> &callback) {
    auto on_message = [callback](const websocketpp::connection_hdl &, const raw_message &msg) {
        auto str = msg->get_payload();
        callback(str);
    };
    server_.set_message_handler(on_message);
}

}  // namespace hgdb
