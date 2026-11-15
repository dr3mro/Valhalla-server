#pragma once

#include <condition_variable>
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

// A small fixed pool of threads for work that is deliberately slow (password hashing and verification), so it never runs on
// a Drogon IO thread. The queue is bounded: submit() returns false when it is full instead of blocking, and the caller
// answers 503.
//
// With zero workers every task runs inline, on the submitting thread. Unit tests use that so they stay synchronous.
class PasswordWorker
{
   public:
    PasswordWorker();  // sizes come from the Configurator (PASSWORD_WORKERS, PASSWORD_QUEUE_MAX)
    PasswordWorker(std::size_t workers, std::size_t queue_max);
    PasswordWorker(const PasswordWorker&)            = delete;
    PasswordWorker(PasswordWorker&&)                 = delete;
    PasswordWorker& operator=(const PasswordWorker&) = delete;
    PasswordWorker& operator=(PasswordWorker&&)      = delete;
    ~PasswordWorker();  // finishes the queued tasks, then joins the threads

    // True when the task was accepted (or ran inline). False when the queue is full or the pool is stopping.
    [[nodiscard]] bool submit(std::function<void()> task);

    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] bool        inlineMode() const { return workers_.empty(); }

   private:
    void run();

    mutable std::mutex                mutex_;
    std::condition_variable           cv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread>          workers_;
    std::size_t                       queue_max_;
    bool                              stopping_ = false;
};

// Runs body(callback) on the worker. When the queue is full the callback is answered right away with 503 and body never
// runs. The callback is shared with the task so that it is still available for that reply.
template <typename Body>
void runOnPasswordWorker(PasswordWorker& worker, CALLBACK_&& callback, Body&& body)
{
    auto shared_callback = std::make_shared<CALLBACK_>(std::move(callback));
    if (!worker.submit([shared_callback, task = std::forward<Body>(body)]() mutable { task(std::move(*shared_callback)); }))
    {
        (*shared_callback)(api::v2::Http::Status::SERVICE_UNAVAILABLE, "Too many password operations in progress, try again shortly.");
    }
}
