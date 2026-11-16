/**
 * @file dbexecutor.hpp
 * @brief BoundedExecutor configured for the synchronous database path.
 * @details This header declares DbExecutor, a thin subclass of BoundedExecutor
 * that runs blocking database work off the Drogon IO threads. It has two
 * constructors: one takes worker count, queue size and fast-path flag from
 * configuration, the other takes explicit sizes for tests. It also provides
 * runOnDbExecutor(), a helper that submits work and answers 503 itself if the
 * bounded queue is full.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Drogon's IO threads must stay free to accept and route HTTP requests; a slow
 * SQL query on one of them would stall every other request. Routes obtain the
 * shared DbExecutor from the Store and call runOnDbExecutor() so permission
 * checks, queries and response building run on dedicated workers. The optional
 * IO-thread fast path (see dbprobe.hpp) lets requests that need no database
 * finish without ever queueing. Without this executor, blocking database work
 * would run on IO threads and the server would grind to a halt under load.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Inheritance / derived classes:
 * https://en.cppreference.com/w/cpp/language/derived_class
 * - Function templates:
 * https://en.cppreference.com/w/cpp/language/function_template
 * - Perfect forwarding (std::forward):
 * https://en.cppreference.com/w/cpp/utility/forward
 * - std::size_t (an unsigned size type):
 * https://en.cppreference.com/w/cpp/types/size_t
 * - Thread pools and bounded queues (back-pressure)
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: declare this class at most once per translation
              // unit.

// std::size_t is used for the worker count and queue capacity.
#include <cstddef>
// std::move and std::forward pass the callback and body efficiently.
#include <utility>

// BoundedExecutor is the base class that implements the worker pool.
#include "concurrency/boundedexecutor.hpp"
// CALLBACK_ is the status+message callback type passed through the executor.
#include "utils/global/callback.hpp"

// The pool that runs blocking database work (permission check, query, response)
// so a slow query never blocks a Drogon IO thread. Size: DB_WORKERS (default
// DB_MAX_CONN, so each worker holds at most one connection), queue:
// DB_QUEUE_MAX.
/**
 * @class DbExecutor
 * @brief BoundedExecutor configured for the synchronous database path.
 * @details Runs blocking database work (permission checks, queries, response
 * building) off the Drogon IO threads. Worker count and queue capacity come
 * from configuration in the default constructor; the other constructor allows
 * explicit sizes for tests. When configured, requests may first be attempted on
 * the IO thread via a DbProbe (see ioFastPath()). Thread-safety and shutdown
 * semantics are inherited from BoundedExecutor.
 * @see BoundedExecutor, DbProbe, runOnDbExecutor
 */
class DbExecutor
    // Reuses the generic worker pool; only the
    // database-specific setup is added here.
    : public BoundedExecutor {
 public:
  /**
   * @brief Constructs the executor sized and configured from the
   * Configurator.
   */
  DbExecutor();  // sizes come from the Configurator

  /**
   * @brief Constructs an executor with explicit sizes (used by tests).
   * @param[in] workers Number of worker threads; zero runs tasks inline on
   * the submitter.
   * @param[in] queue_max Maximum number of queued tasks across all workers.
   * @param[in] io_fast_path Whether to try requests on the IO thread before
   * queuing.
   */
  DbExecutor(std::size_t workers,
             std::size_t queue_max,
             bool io_fast_path = false);  // The default keeps the fast path off
                                          // unless explicitly requested.

  // True when requests are first tried on the IO thread (see
  // database/dbprobe.hpp) and only queued if they need the database.
  /**
   * @brief Reports whether the IO-thread fast path is active.
   * @return `true` only when configured and the executor is not in inline
   * mode.
   */
  [[nodiscard]] bool ioFastPath() const {
    return io_fast_path_ && !inlineMode();
  }  // Inline mode (zero workers) has no IO thread to offload from, so the
     // fast path is pointless there.

 private:
  /**
   * Set from configuration; enables the IO-thread probe when workers exist.
   */
  bool io_fast_path_ = false;  // Defaults to off so a plain executor never
                               // surprises callers with probe behaviour.
};

// Runs body(callback) on the DB workers; a full queue answers 503 and body
// never runs.
/**
 * @brief Submits `body` to a DbExecutor, answering 503 if it cannot be
 * accepted.
 * @details Wraps runOnExecutor with the database-busy message. The callback is
 * shared with the queued task so it remains available for the 503 reply if
 * submission fails. Any database work that must not run on an IO thread belongs
 * in `body`.
 * @tparam Body Callable taking a `CALLBACK_` by value/rvalue reference.
 * @param[in,out] executor Executor that runs the body.
 * @param[in,out] callback Completion callback invoked with (status, message).
 * @param[in,out] body Work to run on a worker; not invoked when the queue is
 * full.
 * @post Exactly one of `body` (on a worker) or `callback` with 503 (on the
 * caller) runs.
 */
template <typename Body>
void runOnDbExecutor(DbExecutor& executor, CALLBACK_&& callback, Body&& body) {
  runOnExecutor(executor,
                std::move(callback),
                "The server is busy, try again shortly.",
                std::forward<Body>(
                    body));  // Reuse the generic helper; the busy message here
                             // tells clients to retry when the DB queue is
                             // full.
}
