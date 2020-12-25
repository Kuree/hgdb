#include "thread.hh"

namespace hgdb {

void RuntimeLock::wait() {
    cv_.wait(lock_, [this] { return ready_.load(); });
}

void RuntimeLock::ready() {
    if (!ready_.load()) {
        ready_ = true;
        lock_.unlock();
        cv_.notify_one();
    }
}

}  // namespace hgdb