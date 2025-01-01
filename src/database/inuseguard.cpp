#include "database/inuseguard.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>

InUseGuard::InUseGuard(std::atomic<bool>& in_use, std::mutex& mtx, std::condition_variable& _cv) : isConnectionInUse_(in_use), mutex_(mtx), conditionVar_(_cv)
{
    std::unique_lock<std::mutex> lock(mutex_);

    conditionVar_.wait(lock, [this] { return !isConnectionInUse_.load(); });

    isConnectionInUse_.store(true);
}

InUseGuard::~InUseGuard()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isConnectionInUse_.store(false);
    }
    conditionVar_.notify_all();
}
