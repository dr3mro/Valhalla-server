#include "concurrency/boundedexecutor.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "utils/message/message.hpp"

namespace
{
    // Set while a thread is running tasks for an executor; lets onWorkerThread() answer without a lookup table.
    thread_local const BoundedExecutor* currentExecutor = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}  // namespace

BoundedExecutor::BoundedExecutor(std::size_t workers, std::size_t queue_max, std::string name) : queue_max_(std::max<std::size_t>(queue_max, 1)), name_(std::move(name))
{
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i)
    {
        workers_.emplace_back([this] { run(); });
    }
}

BoundedExecutor::~BoundedExecutor()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

bool BoundedExecutor::submit(std::function<void()> task)
{
    if (inlineMode())
    {
        task();
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= queue_max_)
        {
            return false;
        }
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
    return true;
}

std::size_t BoundedExecutor::pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool BoundedExecutor::onWorkerThread() const { return currentExecutor == this; }

void BoundedExecutor::run()
{
    currentExecutor = this;
    for (;;)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty())
            {
                return;  // stopping and nothing left to do
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }

        try
        {
            task();
        }
        catch (const std::exception& e)
        {
            // A task answers its own request; this only keeps a stray exception from ending the thread.
            Message::CriticalMessage(name_ + " task failed: " + e.what());
        }
        catch (...)
        {
            Message::CriticalMessage(name_ + " task failed with an unknown exception.");
        }
    }
}
