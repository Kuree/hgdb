#include "log.hh"

#include <chrono>
#include <iostream>

#include "fmt/format.h"

// adapted from https://gist.github.com/polaris/adee936198995a6f8c697c419d21f734
static std::string to_string(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::string ts = std::ctime(&t);
    ts.resize(ts.size() - 1);
    return ts;
}

namespace hgdb::log {
void log(log_level level, const std::string& msg) {
    auto tag = level == log_level::error ? "ERROR" : "INFO";
    auto str = fmt::format("[{0}][{1}] {2}", tag, to_string(std::chrono::system_clock::now()), msg);
    if (level == log_level::error) {
        std::cerr << str << std::endl;
    } else {
        std::cout << str << std::endl;
    }
}
}  // namespace hgdb::log