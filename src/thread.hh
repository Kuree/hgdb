#ifndef HGDB_THREAD_HH
#define HGDB_THREAD_HH

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace hgdb {

class RuntimeLock {
public:
    RuntimeLock() = default;
    void wait();
    void ready();

private:
    std::mutex m_;
    std::atomic<bool> ready_ = false;
    std::condition_variable cv_;
};

}  // namespace hgdb

#endif  // HGDB_THREAD_HH
