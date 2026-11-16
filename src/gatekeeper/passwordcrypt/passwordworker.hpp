/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file passwordworker.hpp
 * @brief A dedicated thread pool for the slow work of hashing and checking
 * passwords. (Originally: "The pool for deliberately slow password work
 * (hashing and verification), so it never runs on a Drogon IO thread.")
 * @details This header declares a small class that is really just a named
 *          BoundedExecutor for password jobs, plus a helper that submits a job
 *          to it. Password work is deliberately slow, so it must not run on the
 *          threads that handle web requests.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Login and registration need password hashing, which is intentionally
 * expensive. If that ran on a Drogon IO thread, one login would freeze request
 * handling for everyone. GateKeeper uses this pool to keep password work
 * separate; without it the server would be slow and vulnerable to login-based
 * denial of service.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Thread pools: https://en.wikipedia.org/wiki/Thread_pool
 * - C++ threads: https://en.cppreference.com/w/cpp/thread/thread
 * - HTTP 503 (server busy):
 * https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/503
 * - Perfect forwarding (std::forward):
 * https://en.cppreference.com/w/cpp/utility/forward
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <cstddef>  // std::size_t for the worker and queue counts.
// std::move and std::forward for passing the job and callback along.
#include <utility>

// The base class that owns the queue and worker threads.
#include "concurrency/boundedexecutor.hpp"
// CALLBACK_, the type used to answer the HTTP request.
#include "utils/global/callback.hpp"

/**
 * @class PasswordWorker
 * @brief The pool for deliberately slow password work (hashing and
 * verification), so it never runs on a Drogon IO thread.
 * @details Keeping password operations off the IO threads prevents them from
 * blocking request handling. Queueing, fair dispatch, zero-worker inline mode
 * and shutdown semantics are inherited from BoundedExecutor; see that class for
 * the queue and inline-mode rules. Think of it as a queue with a fixed number
 * of helpers: requests drop a job in the queue, and a free helper picks it up.
 * @see BoundedExecutor, runOnPasswordWorker
 */
class PasswordWorker
    : public BoundedExecutor  // Inherits all queueing and thread behaviour from
                              // BoundedExecutor.
{
 public:  // The following constructors are available to callers.
  /** @brief Constructs the pool sized from Configurator (PASSWORD_WORKERS,
   * PASSWORD_QUEUE_MAX). */
  PasswordWorker();  // Sizes come from the Configurator (PASSWORD_WORKERS,
                     // PASSWORD_QUEUE_MAX).

  /**
   * @brief Constructs the pool with explicit sizes (mainly for tests).
   * @param[in] workers Number of worker threads; zero enables inline mode.
   * @param[in] queue_max Maximum number of queued tasks across all workers.
   */
  PasswordWorker(
      std::size_t workers,
      std::size_t queue_max);  // Explicit sizes, mainly used by tests.
};

/**
 * @brief Runs @p body on @p worker, answering the callback with 503 if the
 * queue is full.
 * @details The callback is shared with the queued task so it remains valid for
 * the busy reply; in that case @p body never executes. See runOnExecutor() for
 * the full contract. In everyday words: try to put this job on the password
 * pool. If the queue is full, immediately answer the client with HTTP 503 ("try
 *          again soon") instead of making them wait forever.
 * @tparam Body Callable invoked with a moved CALLBACK_ on a worker thread.
 * @param[in,out] worker Pool that executes @p body.
 * @param[in,out] callback Request callback moved into the task (or used for the
 * 503 reply).
 * @param[in,out] body Work to run on the password worker.
 */
template <typename Body>
void runOnPasswordWorker(PasswordWorker& worker,
                         CALLBACK_&&     callback,
                         Body&& body)  // Submits a password job to the pool.
{
  // Hand off to the generic helper with a password-specific "busy" message.
  // std::forward keeps the job's value category intact.
  runOnExecutor(worker,
                std::move(callback),
                "Too many password operations in progress, try again shortly.",
                std::forward<Body>(
                    body));  // The real work of queueing and replying.
}
