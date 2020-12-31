#include "thread.hh"

namespace hgdb {

void RuntimeLock::wait() {
    std::unique_lock<std::mutex> lock_(m_);
    cv_.wait(lock_, [this] { return ready_.load(); });
    ready_.store(false);
}

void RuntimeLock::ready() {
    if (!ready_.load()) {
        {
            std::unique_lock<std::mutex> lock_(m_);
            ready_ = true;
        }
        cv_.notify_one();
    }
}

}  // namespace hgdb