#include <chrono>
#include <iostream>
#include <thread>

#include "../src/server.hh"

constexpr auto stop_msg = "stop";

int main(int argc, char* argv[]) {
    using namespace std::chrono_literals;

    if (argc != 2) return EXIT_FAILURE;
    uint16_t port = std::stoul(argv[1]);
    std::cout << "Using port " << port << std::endl;
    // enable logging for testing/debugging
    hgdb::DebugServer server(port, true);
    // need to defer the shutdown using a thread
    std::thread t;
    // make it an echo server
    auto echo = [&server, &t](const std::string& msg) {
        server.send(msg);
        // if the message is stop, we stop the server
        if (msg == stop_msg) {
            printf("shutting down\n");
            t = std::thread([&server]() {
                // defer the shutdown for 1s to allow the either side
                // to receive
                std::this_thread::sleep_for(0.5s);
                server.stop();
            });
        }
    };
    server.set_on_message(echo);
    server.run();
    t.join();

    return EXIT_SUCCESS;
}