#include "database/inuseguard.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

InUseGuard::InUseGuard(std::atomic<bool>& in_use, std::mutex& mtx, std::condition_variable& _cv, std::chrono::milliseconds timeout)
    : isConnectionInUse_(in_use), mutex_(mtx), conditionVar_(_cv)
{
    std::unique_lock<std::mutex> lock(mutex_);

    const bool acquired = conditionVar_.wait_for(lock, timeout, [this]() { return !isConnectionInUse_.load(); });
    if (!acquired)
    {
        throw std::runtime_error("Timed out waiting for database connection");
    }

    isConnectionInUse_.store(true);
}

InUseGuard::~InUseGuard()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        isConnectionInUse_.store(false);
    }
    conditionVar_.notify_all();
}
