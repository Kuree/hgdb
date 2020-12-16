#include <iostream>

#include "../src/server.hh"

constexpr auto stop_msg = "stop";

int main(int argc, char* argv[]) {
    if (argc != 2) return EXIT_FAILURE;
    uint16_t port = std::stoul(argv[1]);
    hgdb::DebugServer server(port);
    // make it echo server
    auto echo = [&server](const std::string &msg) {
        server.send(msg);
        // if the message is stop, we stop the server
        if (msg == stop_msg) {
            server.stop();
        }
    };
    server.set_on_message(echo);
    server.run();

    return EXIT_SUCCESS;
}