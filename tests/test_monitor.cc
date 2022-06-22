#include "../src/monitor.hh"
#include "gtest/gtest.h"

TEST(monitor, get_watched_values) {  // NOLINT
    using vpiHandle = unsigned int *;
    int64_t value_a = 42, value_b = 43, value_c = 44;
    // NOLINTNEXTLINE
    auto get_value = [&value_a, &value_b, &value_c](vpiHandle handle) -> int64_t {
        const char *base = (char *)nullptr;
        if (handle == vpiHandle(base + 1)) return value_a;
        if (handle == vpiHandle(base + 2)) return value_b;
        if (handle == vpiHandle(base + 3)) return value_c;
        return 0;
    };

    auto get_handle = [](const std::string &name) -> hgdb::Monitor::vpiHandle {
        if (name == "a") return vpiHandle(1);
        if (name == "b") return vpiHandle(2);
        if (name == "c") return vpiHandle(3);
        return nullptr;
    };
    hgdb::Monitor monitor(get_value, get_handle);
    monitor.add_monitor_variable("a", hgdb::Monitor::WatchType::breakpoint);
    monitor.add_monitor_variable("b", hgdb::Monitor::WatchType::clock_edge);
    monitor.add_monitor_variable("c", hgdb::Monitor::WatchType::changed);
    {
        auto values = monitor.get_watched_values(hgdb::Monitor::WatchType::breakpoint);
        EXPECT_EQ(values.size(), 1);
        EXPECT_EQ(values.begin()->second, 42);
    }
    {
        auto values = monitor.get_watched_values(hgdb::Monitor::WatchType::clock_edge);
        EXPECT_EQ(values.size(), 1);
        EXPECT_EQ(values.begin()->second, 43);
    }
    {
        auto values = monitor.get_watched_values(hgdb::Monitor::WatchType::changed);
        EXPECT_EQ(values.size(), 1);
        EXPECT_EQ(values.begin()->second, 44);
        values = monitor.get_watched_values(hgdb::Monitor::WatchType::changed);
        EXPECT_EQ(values.size(), 0);
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