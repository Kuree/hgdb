#include "../src/monitor.hh"
#include "gtest/gtest.h"

TEST(monitor, get_watched_values) {  // NOLINT
    int64_t value_a = 42, value_b = 43;
    auto get_value = [&value_a, &value_b](const std::string &name) -> int64_t {
        if (name == "a") return value_a;
        if (name == "b") return value_b;
        return 0;
    };
    hgdb::Monitor monitor(get_value);
    monitor.add_monitor_variable("a", hgdb::Monitor::WatchType::breakpoint);
    monitor.add_monitor_variable("b", hgdb::Monitor::WatchType::clock_edge);
    {
        auto values = monitor.get_watched_values(true);
        EXPECT_EQ(values.size(), 1);
        EXPECT_EQ(values.begin()->second, "42");
    }
    {
        auto values = monitor.get_watched_values(false);
        EXPECT_EQ(values.size(), 1);
        EXPECT_EQ(values.begin()->second, "43");
    }
}

TEST(monitor, remove_track) {  // NOLINT
    hgdb::Monitor monitor;
    // once switch to gcc-11, we will use the following syntax
    // using enum hgdb::Monitor::WatchType;
    auto const id1 = monitor.add_monitor_variable("a", hgdb::Monitor::WatchType::breakpoint);
    auto const id2 = monitor.add_monitor_variable("a", hgdb::Monitor::WatchType::breakpoint);
    auto const id3 = monitor.add_monitor_variable("b", hgdb::Monitor::WatchType::breakpoint);
    EXPECT_FALSE(monitor.empty());
    EXPECT_EQ(monitor.num_watches("a", hgdb::Monitor::WatchType::breakpoint), 1);
    // id1 and id2 will be the same
    EXPECT_EQ(id1, id2);

    monitor.remove_monitor_variable(id1);
    EXPECT_EQ(monitor.num_watches("a", hgdb::Monitor::WatchType::breakpoint), 0);

    monitor.remove_monitor_variable(id3);
    EXPECT_TRUE(monitor.empty());
}