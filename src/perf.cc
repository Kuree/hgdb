#include "perf.hh"

#include <fstream>
#include <iomanip>
#include <iostream>

#include "util.hh"

auto constexpr DEBUG_PERF_COUNT_NAME = "DEBUG_PERF_COUNT_NAME";

namespace hgdb::perf {

// static variable initialization
std::unordered_map<std::string_view, double> PerfCount::counts_ = {};
std::mutex PerfCount::count_mutex_ = {};

PerfCount::PerfCount(std::string_view name, bool collect) : collect_(collect) {
#ifdef PERF_COUNT
    if (collect) [[likely]] {
        name_ = name;
        start_ = std::chrono::high_resolution_clock::now();
    }
#else
    (void)name;
    (void)collect;
#endif
}

// need to do this because when the perf is not turned on, this is a compiler warning
// NOLINTNEXTLINE
PerfCount::~PerfCount() {
#ifdef PERF_COUNT
    if (collect_) [[likely]] {
        // time measurement is not on the locked path
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::nano> diff = (end - start_);
        {
            std::lock_guard guard(count_mutex_);
            if (counts_.find(name_) == counts_.end()) [[unlikely]] {
                counts_.emplace(name_, 0);
            }
            counts_[name_] += diff.count();
        }
    }
#endif
}

void PerfCount::print_out(std::string_view filename) {
    std::ostream *os = &std::cout;
    std::ofstream fs;
    if (!filename.empty()) {
        fs.open(filename.data(), std::ios_base::app);
        if (!fs.bad()) {
            os = &fs;
        }
    }

    // dump the name if possible
    if (auto name = util::getenv(DEBUG_PERF_COUNT_NAME)) {
        (*os) << *name << std::endl;
    }

    // first pass to determine the max length
    std::lock_guard guard(count_mutex_);
    int s = 0;
    for (auto const &[n, c] : counts_) {
        auto ns = static_cast<int>(n.size());
        if (ns > s) s = ns;
    }

    for (auto const &[n, c] : counts_) {
        auto d = c / std::nano::den;
        (*os) << std::left << std::setw(s) << n << ": " << std::fixed << std::setprecision(9) << d
              << "s" << std::endl;
    }

    // clean up
    if (os != &std::cout) {
        fs.close();
    }
}

}  // namespace hgdb::perf