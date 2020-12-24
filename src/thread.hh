#ifndef HGDB_THREAD_HH
#define HGDB_THREAD_HH

#include <condition_variable>
#include <mutex>
#include <atomic>

namespace hgdb {

class RuntimeLock {
public:
    RuntimeLock() : lock_(m_) {}
    void wait();
    void ready();

private:
    std::mutex m_;
    std::unique_lock<std::mutex> lock_;
    std::atomic<bool> ready_ = false;
    std::condition_variable cv_;
};

}  // namespace hgdb

#endif  // HGDB_THREAD_HH
