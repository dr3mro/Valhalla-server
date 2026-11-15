#include "concurrency/boundedexecutor.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
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
        workers_.push_back(std::make_unique<Worker>());
    }
    // Start the threads only now: a worker looks at every queue, so all of them must exist first.
    for (std::size_t i = 0; i < workers; ++i)
    {
        workers_[i]->thread = std::thread([this, i] { run(i); });
    }
}

BoundedExecutor::~BoundedExecutor() { shutdown(); }

void BoundedExecutor::shutdown()
{
    stopping_.store(true, std::memory_order_release);
    for (auto& worker : workers_)
    {
        {
            std::lock_guard<std::mutex> lock(worker->mutex);  // a sleeping worker either sees the flag or is already waiting
        }
        worker->cv.notify_all();
    }
    for (auto& worker : workers_)
    {
        if (worker->thread.joinable() && worker->thread.get_id() != std::this_thread::get_id())
        {
            worker->thread.join();
        }
    }

    const Stats s = stats();
    if (s.submitted > 0 || s.rejected > 0)
    {
        Message::InfoMessage(fmt::format("{}: {} tasks, {} rejected, deepest queue {}, average wait {:.1f} us.", name_, s.submitted, s.rejected, s.max_queue, s.avg_wait_us));
        submitted_ = 0;  // report once
        rejected_  = 0;
    }
}

BoundedExecutor::Stats BoundedExecutor::stats() const
{
    const auto waited = waited_tasks_.load(std::memory_order_relaxed);
    return {submitted_.load(std::memory_order_relaxed), rejected_.load(std::memory_order_relaxed), max_queue_.load(std::memory_order_relaxed),
        waited == 0 ? 0.0 : static_cast<double>(wait_ns_.load(std::memory_order_relaxed)) / static_cast<double>(waited) / 1000.0};
}

bool BoundedExecutor::submit(std::function<void()> task)
{
    if (inlineMode())
    {
        task();
        return true;
    }
    if (stopping_.load(std::memory_order_acquire))
    {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Reserve a place in the shared limit first; give it back if the pool turns out to be stopping.
    const std::size_t depth = queued_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (depth > queue_max_)
    {
        queued_.fetch_sub(1, std::memory_order_acq_rel);
        rejected_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // From a round-robin start: the first idle worker; with none idle, the one with the shortest queue (the start worker wins ties).
    const std::size_t n        = workers_.size();
    const std::size_t start    = next_.fetch_add(1, std::memory_order_relaxed) % n;
    std::size_t       target   = start;
    std::size_t       shortest = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t candidate = (start + i) % n;
        if (workers_[candidate]->idle.load(std::memory_order_acquire))
        {
            target = candidate;
            break;
        }
        const std::size_t depth = workers_[candidate]->depth.load(std::memory_order_relaxed);
        if (depth < shortest)
        {
            shortest = depth;
            target   = candidate;
        }
    }

    Worker& worker = *workers_[target];
    bool    wake   = false;
    {
        std::lock_guard<std::mutex> lock(worker.mutex);
        if (stopping_.load(std::memory_order_acquire))  // checked under the lock: the worker's exit check is under it too
        {
            queued_.fetch_sub(1, std::memory_order_acq_rel);
            rejected_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        worker.queue.push_back({std::move(task), std::chrono::steady_clock::now()});
        worker.depth.store(worker.queue.size(), std::memory_order_relaxed);
        // An idle worker is claimed (flag cleared) so the next submit() picks another one instead of queueing behind this task.
        // A busy worker finds its task itself; it needs no wake-up (system call).
        wake = worker.idle.exchange(false, std::memory_order_acq_rel);
    }
    submitted_.fetch_add(1, std::memory_order_relaxed);
    if (depth > max_queue_.load(std::memory_order_relaxed))
    {
        max_queue_.store(depth, std::memory_order_relaxed);
    }
    if (wake)
    {
        worker.cv.notify_one();
    }
    return true;
}

bool BoundedExecutor::onWorkerThread() const { return currentExecutor == this; }

bool BoundedExecutor::take(std::size_t self, Item& item)
{
    {
        Worker&                     own = *workers_[self];
        std::lock_guard<std::mutex> lock(own.mutex);
        if (!own.queue.empty())
        {
            item = std::move(own.queue.front());
            own.queue.pop_front();
            own.depth.store(own.queue.size(), std::memory_order_relaxed);
            queued_.fetch_sub(1, std::memory_order_acq_rel);
            return true;
        }
    }
    // Nothing of our own: help a worker that is stuck on a slow task. try_lock keeps this from ever waiting on someone.
    const std::size_t n = workers_.size();
    for (std::size_t i = 1; i < n; ++i)
    {
        Worker&                      other = *workers_[(self + i) % n];
        std::unique_lock<std::mutex> lock(other.mutex, std::try_to_lock);
        if (lock.owns_lock() && !other.queue.empty())
        {
            item = std::move(other.queue.front());
            other.queue.pop_front();
            other.depth.store(other.queue.size(), std::memory_order_relaxed);
            queued_.fetch_sub(1, std::memory_order_acq_rel);
            return true;
        }
    }
    return false;
}

void BoundedExecutor::account(const Item& item)
{
    wait_ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - item.queued_at).count()),
        std::memory_order_relaxed);
    waited_tasks_.fetch_add(1, std::memory_order_relaxed);
}

void BoundedExecutor::run(std::size_t self)
{
    currentExecutor = this;
    Worker& own     = *workers_[self];
    for (;;)
    {
        Item item;
        if (!take(self, item))
        {
            std::unique_lock<std::mutex> lock(own.mutex);
            if (own.queue.empty())
            {
                if (stopping_.load(std::memory_order_acquire))
                {
                    return;  // stopping, and nothing is left in our queue (no submit can add one any more)
                }
                // idle is set again on every pass: a submit() that claimed us may have lost its task to a thief, and then
                // the wake-up finds an empty queue; going back to sleep with idle cleared would hide us from every later submit().
                while (!stopping_.load(std::memory_order_acquire) && own.queue.empty())
                {
                    own.idle.store(true, std::memory_order_release);
                    own.cv.wait(lock);
                }
                own.idle.store(false, std::memory_order_release);
            }
            continue;  // look again: a task arrived, or we are stopping
        }

        account(item);

        try
        {
            item.task();
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
