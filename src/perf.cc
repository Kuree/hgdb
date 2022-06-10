#include "perf.hh"

#include <iomanip>
#include <iostream>

#include "fmt/format.h"

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

void PerfCount::print_out() {
    // first pass to determine the max length
    std::lock_guard guard(count_mutex_);
    int s = 0;
    for (auto const &[n, c] : counts_) {
        auto ns = static_cast<int>(n.size());
        if (ns > s) s = ns;
    }

    for (auto const &[n, c] : counts_) {
        auto d = c / std::nano::den;
        std::cout << std::left << std::setw(s) << n << ": " << std::fixed << std::setprecision(9)
                  << d << "s" << std::endl;
    }
}

}  // namespace hgdb::perf