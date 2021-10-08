#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <chrono>
#include <thread>

#include "../src/sim.hh"
#include "../src/thread.hh"
#include "gtest/gtest.h"
#include "test_util.hh"

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

uint16_t get_free_port() {
    int fd;
    struct sockaddr_in addr {
        .sin_family = AF_INET, .sin_port = 0
    };
    addr.sin_addr.s_addr = inet_addr("0.0.0.0");
    fd = socket(AF_INET, SOCK_STREAM, 0);
    auto res = bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (res) return 8888;
    // set socket options
    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
    socklen_t sa_len = sizeof(addr);
    res = getsockname(fd, (struct sockaddr *)&addr, &sa_len);
    if (res) return 8888;
    close(fd);
    auto port = addr.sin_port;
    auto result = ntohs(port);
    return result;
}

TEST(sim, sim_cb) {  // NOLINT
    // notice that this test doesn't check if the server is behaving correctly
    // it only checks if the call backs has been set up properly!
    // need real-world end-to-end case to test if it's working!

    // need to get a free port
    auto port = get_free_port();
    auto mock = std::make_unique<MockVPIProvider>();
    // even through the ownership gets transferred, as long as we don't
    // tear down the runtime, vpi_ptr is always valid
    auto *vpi_ptr = mock.get();
    vpi_ptr->set_argv({"+DEBUG_PORT=" + std::to_string(port)});

    std::unique_ptr<hgdb::AVPIProvider> vpi_ = std::move(mock);
    auto *debugger = hgdb::initialize_hgdb_runtime_vpi(std::move(vpi_), false);

    auto t1 = std::thread([vpi_ptr]() {
        vpi_ptr->trigger_cb(cbStartOfSimulation);
    });

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(400ms);

    using Client = websocketpp::client<websocketpp::config::asio_client>;
    Client client;
    client.init_asio();

    websocketpp::lib::error_code ec;
    auto conn = client.get_connection("ws://localhost:" + std::to_string(port), ec);

    client.connect(conn);

    client.run_one();
    auto b = client.stopped();
    EXPECT_FALSE(b);

    vpi_ptr->stop();

    t1.join();

    // need to delete it manually since we don't have 
    delete debugger;
}