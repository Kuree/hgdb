#include "../src/monitor.hh"
#include "gtest/gtest.h"
#include "test_util.hh"

TEST(monitor, get_watched_values) {  // NOLINT
    auto mock = std::make_shared<MockVPIProvider>();
    hgdb::RTLSimulatorClient rtl(mock);
    int64_t value_a = 42, value_b = 43, value_c = 44;
    auto *a = mock->add_signal(nullptr, "a");
    auto *b = mock->add_signal(nullptr, "b");
    auto *c = mock->add_signal(nullptr, "c");
    mock->set_signal_value(a, value_a);
    mock->set_signal_value(b, value_b);
    mock->set_signal_value(c, value_c);
    hgdb::Monitor monitor(&rtl);
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
    auto mock = std::make_shared<MockVPIProvider>();
    hgdb::RTLSimulatorClient rtl(mock);
    auto *a = mock->add_signal(nullptr, "a");
    auto *b = mock->add_signal(nullptr, "b");
    hgdb::Monitor monitor(&rtl);
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