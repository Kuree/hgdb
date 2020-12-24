#include <thread>
#include <chrono>

#include "../src/thread.hh"
#include "gtest/gtest.h"

TEST(thread, wait_ready) {  // NOLINT
    using namespace std::chrono_literals;
    bool state = false;
    hgdb::RuntimeLock lock;
    auto t = std::thread([&state, &lock]() {
        lock.wait();
        state = true;
    });
    std::this_thread::sleep_for(10ms);
    EXPECT_FALSE(state);
    lock.ready();
    std::this_thread::sleep_for(10ms);
    EXPECT_TRUE(state);
    t.join();
}