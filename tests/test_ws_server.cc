#include <array>
#include <chrono>
#include <iostream>
#include <thread>

#include "../src/server.hh"

constexpr auto stop_msg = "stop";
constexpr auto topic_msg = "42";
// for some reason gcc-10 and clang-11 still can't support constexpr std::string
// specified in C++20
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0980r1.pdf
// https://godbolt.org/z/K3sjP7
// will remove the std::string conversion hack once it's officially supported
constexpr auto debug_port = "+DEBUG_PORT=";

int main(int argc, char* argv[]) {
    using namespace std::chrono_literals;

    if (argc != 2) return EXIT_FAILURE;
    std::string arg_port = argv[1];
    std::string debug_port_str = debug_port;
    auto port_str = arg_port.substr(arg_port.find(debug_port_str) + debug_port_str.size());
    uint16_t port = std::stoul(port_str);
    std::cout << "Using port " << port << std::endl;
    // enable logging for testing/debugging
    hgdb::DebugServer server(true);
    // need to defer the shutdown using a thread
    std::thread t;
    // make it an echo server
    auto echo = [&server, &t](const std::string& msg, uint64_t conn_id) {
        // this is broadcast
        server.send(msg);

        // publish to modified message to a particular topic
        server.send(msg + msg, topic_msg);
        // if the message is stop, we stop the server
        if (msg == stop_msg) {
            printf("shutting down\n");
            t = std::thread([&server]() {
                // defer the shutdown for 1s to allow the either side
                // to receive
                std::this_thread::sleep_for(0.5s);
                server.stop();
            });
        } else if (msg == topic_msg) {
            // add it to the topic
            server.add_to_topic(topic_msg, conn_id);
        }
    };
    server.set_on_message(echo);
    server.run(port);
    t.join();

    return EXIT_SUCCESS;
}