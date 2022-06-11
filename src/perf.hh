#ifndef HGDB_PERF_HH
#define HGDB_PERF_HH

#include <chrono>
#include <mutex>
#include <ostream>
#include <string_view>
#include <unordered_map>

namespace hgdb::perf {
class PerfCount {
public:
    PerfCount(std::string_view name, bool collect);

    ~PerfCount();

    static void print_out(std::string_view filename);

private:
    std::string_view name_;
    bool collect_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;

    static std::mutex count_mutex_;
    static std::unordered_map<std::string_view, double> counts_;
};

}  // namespace hgdb::perf
#endif  // HGDB_PERF_HH
