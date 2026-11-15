#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"

// A fixed pool of threads with a bounded queue. It exists so that slow, blocking work (database calls, password hashing)
// never runs on a Drogon IO thread. submit() never blocks: it returns false when the queue is full, and the caller answers 503.
//
// Every worker has its own queue and its own lock, so submitters and workers do not all contend on one mutex. submit() picks
// an idle worker if there is one (so a task never waits behind a slow one while another worker sleeps), otherwise the worker
// with the shortest queue, so the queues stay the same length and waiting time stays fair (plain round-robin left some queues
// long and some short, and the tasks in the long ones waited 20 times longer). Only an idle worker is woken (a busy one needs no wake-up, it looks at its queue when it
// finishes). A worker that has run out of work takes tasks from the other workers' queues before it sleeps. The queue limit
// is shared: at most queue_max tasks wait across all workers.
//
// With zero workers every task runs inline, on the submitting thread. Unit tests use that so they stay synchronous.
class BoundedExecutor
{
   public:
    BoundedExecutor(std::size_t workers, std::size_t queue_max, std::string name);
    BoundedExecutor(const BoundedExecutor&)            = delete;
    BoundedExecutor(BoundedExecutor&&)                 = delete;
    BoundedExecutor& operator=(const BoundedExecutor&) = delete;
    BoundedExecutor& operator=(BoundedExecutor&&)      = delete;
    virtual ~BoundedExecutor();  // same as shutdown()

    // Finishes the queued tasks, then joins the threads. Later submit() calls return false. Safe to call more than once.
    // Call it before the things the tasks use (connection pool, Drogon) are torn down.
    void shutdown();

    // True when the task was accepted (or ran inline). False when the queue is full or the pool is stopping.
    [[nodiscard]] bool submit(std::function<void()> task);

    [[nodiscard]] std::size_t pending() const { return queued_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool        inlineMode() const { return workers_.empty(); }
    [[nodiscard]] std::size_t workerCount() const { return workers_.size(); }

    // True when the calling thread is one of this executor's workers. Work that is already on a worker must call
    // the blocking code directly and not submit again (a full queue could then never drain).
    [[nodiscard]] bool onWorkerThread() const;

    // Counters for tuning the pool size; they only count tasks that went through the queue (not inline mode).
    struct Stats
    {
        std::uint64_t submitted;       // tasks accepted
        std::uint64_t rejected;        // tasks refused because the queue was full
        std::uint64_t max_queue;       // deepest the queue has been
        double        avg_wait_us;     // average time a task waited before a worker took it
    };
    [[nodiscard]] Stats stats() const;

   private:
    struct Item
    {
        std::function<void()>                 task;
        std::chrono::steady_clock::time_point queued_at;
    };

    struct Worker
    {
        std::mutex              mutex;
        std::condition_variable cv;
        std::deque<Item>        queue;
        std::atomic<std::size_t> depth{0};   // queue.size(), readable without the lock
        std::atomic<bool>       idle{false};  // set under mutex while the worker waits for work; read without it by submit()
        std::thread             thread;
    };

    void               run(std::size_t self);
    bool               take(std::size_t self, Item& item);  // own queue first, then the others'
    void               account(const Item& item);

    std::vector<std::unique_ptr<Worker>> workers_;
    std::size_t                          queue_max_;
    std::string                          name_;
    std::atomic<bool>                    stopping_{false};
    std::atomic<std::size_t>             next_{0};      // round-robin start for submit()
    std::atomic<std::size_t>             queued_{0};    // tasks waiting in all queues

    std::atomic<std::uint64_t> submitted_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> max_queue_{0};
    std::atomic<std::uint64_t> wait_ns_{0};
    std::atomic<std::uint64_t> waited_tasks_{0};
};

// Runs body(callback) on the executor. When the queue is full the callback is answered right away with 503 and body never
// runs. The callback is shared with the task so that it is still available for that reply.
template <typename Body>
void runOnExecutor(BoundedExecutor& executor, CALLBACK_&& callback, const char* busy_message, Body&& body)
{
    auto shared_callback = std::make_shared<CALLBACK_>(std::move(callback));
    if (!executor.submit([shared_callback, task = std::forward<Body>(body)]() mutable { task(std::move(*shared_callback)); }))
    {
        (*shared_callback)(api::v2::Http::Status::SERVICE_UNAVAILABLE, busy_message);
    }
}
