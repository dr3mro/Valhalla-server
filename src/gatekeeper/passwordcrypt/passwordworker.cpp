#include "gatekeeper/passwordcrypt/passwordworker.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "configurator/configurator.hpp"
#include "store/store.hpp"
#include "utils/message/message.hpp"

namespace
{
    struct Sizes
    {
        std::size_t workers;
        std::size_t queue_max;
    };

    Sizes sizesFromConfig()
    {
        const auto& config = Store::getObject<Configurator>()->get<Configurator::PasswordWorkerConfig>();
        return {config.workers, config.queue_max};
    }
}  // namespace

PasswordWorker::PasswordWorker() : PasswordWorker(sizesFromConfig().workers, sizesFromConfig().queue_max) {}

PasswordWorker::PasswordWorker(std::size_t workers, std::size_t queue_max) : queue_max_(std::max<std::size_t>(queue_max, 1))
{
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i)
    {
        workers_.emplace_back([this] { run(); });
    }
}

PasswordWorker::~PasswordWorker()
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

bool PasswordWorker::submit(std::function<void()> task)
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

std::size_t PasswordWorker::pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void PasswordWorker::run()
{
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
            Message::CriticalMessage(std::string("Password worker task failed: ") + e.what());
        }
        catch (...)
        {
            Message::CriticalMessage("Password worker task failed with an unknown exception.");
        }
    }
}
