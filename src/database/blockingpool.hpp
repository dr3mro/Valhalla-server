#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

// A fixed set of reusable items handed out one caller at a time. acquire() waits up to a timeout and returns nullptr when
// nothing became free. Header-only and independent of the database so it can be unit tested with plain objects.
template <typename T>
class BlockingPool
{
   public:
    BlockingPool()                               = default;
    BlockingPool(const BlockingPool&)            = delete;
    BlockingPool(BlockingPool&&)                 = delete;
    BlockingPool& operator=(const BlockingPool&) = delete;
    BlockingPool& operator=(BlockingPool&&)      = delete;
    ~BlockingPool()                              = default;

    // Adds a new item, or gives one back after use.
    void release(std::shared_ptr<T> item)
    {
        if (item == nullptr)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            items_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    [[nodiscard]] std::shared_ptr<T> acquire(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !items_.empty(); }))
        {
            return nullptr;
        }
        std::shared_ptr<T> item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

    [[nodiscard]] std::size_t idle() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    // Runs func on every idle item, then finish(), all while holding the pool lock (used to reconnect idle connections and
    // wait for them, so none is handed out half way).
    template <typename Func, typename Finish>
    void forEachIdle(Func&& func, Finish&& finish)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : items_)
        {
            func(item);
        }
        finish();
    }

   private:
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<T>> items_;
};
