/**
 * @file boundedexecutor.hpp
 * @brief A fixed-size worker pool with a bounded queue, plus the
 * callback-adapting helper built on it.
 * @details This file declares BoundedExecutor: a group of worker threads that
 * run slow, blocking jobs such as database calls and password hashing away from
 * Drogon's IO threads. Each worker has a short queue. If every queue is full, a
 * new job is refused instead of being allowed to wait forever, so the server
 * can quickly answer 503. The helper runOnExecutor() makes sure the HTTP reply
 * is sent whether the job was accepted or refused.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Drogon's IO threads must stay free to accept new requests. Database and
 * password work would block them, so callers use runOnExecutor() to hand that
 * work to this pool. The pool reports false when full, which lets the caller
 * answer HTTP 503 ("server busy") rather than piling up unlimited work. With
 * zero workers, jobs run inline on the caller, which is how unit tests stay
 * synchronous. Without this file, every slow call would stall the whole web
 * server.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Condition variables: a way for a thread to sleep until another thread wakes
 * it with new work. https://en.cppreference.com/w/cpp/thread/condition_variable
 * - Atomic memory order: rules for when one thread's writes become visible to
 * another. https://en.cppreference.com/w/cpp/atomic/memory_order
 * - Work stealing: an idle worker takes jobs from a busier worker's queue.
 * https://en.wikipedia.org/wiki/Work_stealing
 * - RAII: a local lock object that unlocks a mutex automatically when the scope
 * ends. https://en.cppreference.com/w/cpp/language/raii
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Compiler-enforced header guard: include at most once per
              // translation unit.

// std::atomic: counters that several threads can safely update.
#include <atomic>
// steady_clock / time_point: timestamps used to measure how long a task waited.
#include <chrono>
// std::condition_variable: lets a worker sleep until work arrives.
#include <condition_variable>
#include <cstddef>  // std::size_t: unsigned counts and sizes.
// std::uint64_t: fixed-width integers for the statistics counters.
#include <cstdint>
// std::deque: the double-ended queue that holds waiting tasks.
#include <deque>
// std::function<void()>: a general-purpose callable used as a task.
#include <functional>
// std::make_shared / std::unique_ptr: owning pointers for callbacks and
// workers.
#include <memory>
#include <mutex>   // std::mutex and lock guards: protect each worker's queue.
#include <string>  // std::string: the pool's name used in log messages.
#include <thread>  // std::thread: the real worker threads.
// std::move / std::forward: pass values along without copying.
#include <utility>
#include <vector>  // std::vector: owns the list of workers.

// CALLBACK_: the HTTP completion callback type handed to tasks.
#include "utils/global/callback.hpp"
// api::v2::Http::Status::SERVICE_UNAVAILABLE: the 503 status used on rejection.
#include "utils/global/http.hpp"

/**
 * @class BoundedExecutor
 * @brief Fixed-size worker pool with a shared bounded queue for slow, blocking
 * work.
 *
 * @details A fixed pool of threads with a bounded queue. It exists so that
 * slow, blocking work (database calls, password hashing) never runs on a Drogon
 * IO thread. submit() never blocks: it returns false when the queue is full,
 * and the caller answers 503.
 *
 *          Every worker has its own queue and its own lock, so submitters and
 * workers do not all contend on one mutex. submit() picks an idle worker if
 * there is one (so a task never waits behind a slow one while another worker
 * sleeps), otherwise the worker with the shortest queue, so the queues stay the
 * same length and waiting time stays fair (plain round-robin left some queues
 *          long and some short, and the tasks in the long ones waited 20 times
 * longer). Only an idle worker is woken (a busy one needs no wake-up, it looks
 * at its queue when it finishes). A worker that has run out of work takes tasks
 * from the other workers' queues before it sleeps. The queue limit is shared:
 * at most queue_max tasks wait across all workers.
 *
 *          With zero workers every task runs inline, on the submitting thread.
 * Unit tests use that so they stay synchronous.
 *
 *          In plain words: imagine a fixed number of chefs (worker threads)
 * each with their own order ticket rack (queue). New orders go to a free chef,
 * or to the shortest rack if nobody is free. A chef with no tickets peek at
 * other racks and helps out before going to sleep ("work stealing"). There is
 * one overall limit on how many tickets may wait; past that, submit() says "no"
 * immediately so the caller can send HTTP 503 rather than let the backlog grow
 * without bound.
 * @note Not copyable or movable; instances are kept alive by shared_ptr,
 * typically through the Store.
 * @see DbExecutor, PasswordWorker, runOnExecutor()
 */
class BoundedExecutor
{
 public:
  /**
   * @brief Creates the workers and starts their threads.
   *
   * @details queue_max is clamped to at least 1. With workers == 0 the pool
   * is in inline mode: no threads are started and submit() runs each task
   * immediately on the calling thread. The constructor does not return until
   * every worker has reported itself idle, so an immediate submit() cannot
   * race with thread startup.
   * @param[in] workers Number of worker threads; 0 selects inline mode.
   * @param[in] queue_max Maximum number of queued tasks across all workers;
   * values below 1 become 1.
   * @param[in] name Label used in log messages and statistics output.
   */
  BoundedExecutor(std::size_t workers, std::size_t queue_max, std::string name);
  /**
   * Copy construction is disabled; an executor owns live threads that cannot
   * be duplicated.
   */
  BoundedExecutor(const BoundedExecutor&) = delete;
  /**
   * Move construction is disabled; workers capture the address of the
   * executor, which a move would invalidate.
   */
  BoundedExecutor(BoundedExecutor&&) = delete;
  /**
   * Copy assignment is disabled; see the copy constructor.
   */
  BoundedExecutor& operator=(const BoundedExecutor&) = delete;
  /**
   * Move assignment is disabled; see the move constructor.
   */
  BoundedExecutor& operator=(BoundedExecutor&&) = delete;
  /**
   * @brief Destructor; calls shutdown().
   * @details Any still-queued tasks are drained and the worker threads joined
   * before resources are released.
   */
  virtual ~BoundedExecutor();  // same as shutdown(): drain tasks, join
                               // threads, then destroy members

  // Finishes the queued tasks, then joins the threads. Later submit() calls
  // return false. Safe to call more than once. Call it before the things the
  // tasks use (connection pool, Drogon) are torn down.
  /**
   * @brief Drains queued tasks, joins the workers and reports accumulated
   * statistics once.
   *
   * @details Sets the stopping flag and wakes every worker, then joins those
   * that are not the calling thread (so a task may call shutdown() on its own
   * executor without deadlocking). A later submit() is rejected. Repeated
   * calls are safe: the second pass finds no joinable threads and no
   * un-reported counters.
   * @note Must run before dependencies of the tasks (connection pool, Drogon)
   * are destroyed.
   * @see ~BoundedExecutor()
   */
  void shutdown();

  // True when the task was accepted (or ran inline). False when the queue is
  // full or the pool is stopping.
  /**
   * @brief Offers a task to the pool without waiting for capacity.
   *
   * @details In inline mode task() runs synchronously here. Otherwise a slot
   * in the shared queue limit is reserved first; if that limit is reached, or
   * the pool is stopping, the task is discarded and false is returned. An
   * idle worker is preferred, else the shortest queue is chosen, and only an
   * idle worker is notified.
   * @param[in] task Nullary callable to execute on a worker (or inline).
   * "Nullary" means it takes no arguments.
   * @return true if the task ran inline or was queued; false if it was
   * rejected.
   * @see runOnExecutor()
   */
  [[nodiscard]] bool submit(std::function<void()> task);

  /**
   * Number of tasks currently waiting across all queues; relaxed because it
   * is only a live gauge.
   */
  [[nodiscard]] std::size_t pending() const
  {
    return queued_.load(std::memory_order_relaxed);
  }  // "relaxed" = a rough, cheap count
  /**
   * True when no worker threads exist and tasks run on the submitting thread.
   */
  // No workers at all means every task runs inline on the submitting thread.
  [[nodiscard]] bool inlineMode() const
  {
    return workers_.empty();
  }
  /**
   * Number of worker threads; zero in inline mode.
   */
  [[nodiscard]] std::size_t workerCount() const
  {
    return workers_.size();
  }  // size of the worker vector

  // True when the calling thread is one of this executor's workers. Work that
  // is already on a worker must call the blocking code directly and not
  // submit again (a full queue could then never drain).
  /**
   * @brief Reports whether the caller is executing on one of this executor's
   * worker threads.
   *
   * @details Uses a thread_local pointer rather than a lookup table. Code
   * already running on a worker should call blocking operations directly:
   * re-submitting could prevent a full queue from ever draining.
   * @return true for this executor's workers, false otherwise (including
   * inline mode on the submitting thread).
   */
  [[nodiscard]] bool onWorkerThread() const;

  // Counters for tuning the pool size; they only count tasks that went
  // through the queue (not inline mode).
  /**
   * @struct Stats
   * @brief Snapshot of tuning counters for tasks that passed through the
   * queue.
   *
   * @details Inline-mode executions are not counted. The values are sampled
   * with relaxed atomic loads, so they are approximate while the pool is
   * running. "Relaxed" means the number is read cheaply without any ordering
   *          guarantees, which is fine for a tuning gauge that nobody makes
   * decisions from in real time.
   */
  struct Stats
  {
    std::uint64_t submitted;    // tasks accepted and put into a queue
    std::uint64_t rejected;     // tasks refused because the queue was full (or
                                // the pool was stopping)
    std::uint64_t max_queue;    // deepest any queue got, used to judge
                                // whether the limit is right
    double        avg_wait_us;  // average time in microseconds a task waited
                                // before a worker took it
  };
  /**
   * @brief Samples the tuning counters.
   * @return A Stats snapshot; avg_wait_us is derived from total wait time
   * divided by the number of waited tasks, or zero if no task has been taken
   * by a worker.
   */
  [[nodiscard]] Stats stats() const;

 private:
  /**
   * @struct Item
   * @brief A queued task together with the timestamp used for wait-time
   * accounting.
   */
  struct Item
  {
    std::function<void()> task;  // the job to run; std::function can hold
                                 // any callable with no arguments
    std::chrono::steady_clock::time_point
        queued_at;  // when it was queued, so we can measure how long it
                    // waited
  };

  /**
   * @struct Worker
   * @brief One worker's thread, queue, lock and condition variable.
   *
   * @details Each worker has its own mutex so submitters and workers do not
   * contend on a single lock. depth and idle are atomics so submit() can make
   * scheduling decisions without locking every worker. ("Contend" means
   *          having to wait for the same lock; giving each worker its own
   * reduces that waiting.)
   */
  struct Worker
  {
    std::mutex mutex;  // protects this worker's queue and condition variable
    std::condition_variable
        cv;  // lets the worker sleep until submit() wakes it with new work
    std::deque<Item> queue;  // the waiting tasks; oldest sits at the front
    std::atomic<std::size_t> depth{
        0};  // queue.size(), readable without the lock so submit() can
             // compare queues quickly
    std::atomic<bool> idle{false};  // set under mutex while the worker waits
                                    // for work; read without it by submit()
    std::thread thread;  // the actual OS thread that runs this worker's loop
  };

  /**
   * @brief Worker entry point: repeatedly take and run tasks until stopping
   * and no work remains.
   *
   * @details Sets the thread_local executor marker, catches exceptions thrown
   * by tasks so they cannot kill the thread, and goes idle with a
   * condition-variable wait when its queue and all stealable queues are
   * empty.
   * @param[in] self Index of this worker.
   */
  void run(std::size_t self);
  /**
   * @brief Removes the next task from this worker's queue, stealing from
   * peers if it is empty.
   *
   * @details Stealing uses try_lock so a worker never blocks waiting for a
   * peer that is busy with a slow task.
   * @param[in] self Index of the calling worker.
   * @param[out] item Receives the taken task and its queueing timestamp.
   * @return true if a task was taken, false if no queue had work.
   */
  bool take(std::size_t self,
            Item&       item);  // own queue first, then the others' queues
  /**
   * @brief Accumulates how long a task waited between queueing and being
   * taken.
   * @param[in] item Task whose queued_at timestamp is used.
   */
  void account(const Item& item);

  /**
   * Worker objects, created before and started after all of them exist; empty
   * in inline mode.
   */
  std::vector<std::unique_ptr<Worker>>
      workers_;  // unique_ptr gives each worker a fixed address that threads
                 // can safely capture
  /**
   * Shared queue limit, at least 1.
   */
  std::size_t queue_max_;  // total tasks allowed to wait across every queue
  /**
   * Log/statistics label for this pool.
   */
  std::string name_;  // e.g. "DB" or "Password", printed in log lines
  /**
   * Set by shutdown(); checked by submit() both outside and inside the target
   * worker's lock.
   */
  std::atomic<bool> stopping_{
      false};  // true means "refuse new work and let workers finish"
  std::atomic<std::size_t> next_{
      0};  // round-robin start for submit(), so submissions spread out
  std::atomic<std::size_t> queued_{
      0};  // tasks waiting in all queues, used to enforce the limit

  /**
   * Accepted tasks that went through the queue.
   */
  std::atomic<std::uint64_t> submitted_{
      0};  // counts tasks that were actually queued (or run inline)
  /**
   * Tasks refused because the queue was full or the pool was stopping.
   */
  std::atomic<std::uint64_t> rejected_{
      0};  // counts rejections, so the caller can see how often 503 happened
  /**
   * Largest value queued_ has reached.
   */
  std::atomic<std::uint64_t> max_queue_{
      0};  // the high-water mark, useful when tuning queue_max_
  /**
   * Total wait time in nanoseconds, used to compute avg_wait_us.
   */
  std::atomic<std::uint64_t> wait_ns_{
      0};  // sum of all time tasks spent waiting in a queue
  /**
   * Number of tasks included in wait_ns_.
   */
  std::atomic<std::uint64_t> waited_tasks_{
      0};  // the divisor for the average wait time
};

/**
 * @brief Runs body(callback) on an executor, answering 503 immediately when the
 * queue is full.
 *
 * @details Runs body(callback) on the executor. When the queue is full the
 * callback is answered right away with 503 and body never runs. The callback is
 * shared with the task so that it is still available for that reply.
 *
 *          The callback is moved into a shared_ptr that both the queued task
 * and this function hold. On acceptance the task runs on a worker thread (or
 * inline when the executor has no workers) and body receives the callback as an
 *          rvalue. On rejection the callback is invoked here, on the submitting
 * thread, with SERVICE_UNAVAILABLE and busy_message; body is destroyed without
 * being called.
 *
 *          In plain words: hand the slow job (body) and the HTTP reply function
 * (callback) to the pool. If the pool takes the job, a worker runs body later
 * and body sends the reply. If the pool is full, we send a 503 right now using
 * the same callback. Either way the client always gets an answer, and the
 * callback is never lost.
 * @tparam Body Callable invocable with a CALLBACK_ (std::function<void(int,
 * const std::string&)>); it is captured by the task, so it must be
 * move-constructible.
 * @param[in,out] executor Pool that will execute body.
 * @param[in] callback Completion callback; ownership is moved into this helper.
 * @param[in] busy_message Message paired with the 503 status on rejection.
 * @param[in] body The blocking work to run on the pool.
 * @note Because the callback is moved into the task, callers must not use it
 * after passing it here.
 * @see BoundedExecutor::submit(), runOnDbExecutor(), runOnPasswordWorker()
 */
template <typename Body>
void runOnExecutor(BoundedExecutor& executor,
                   CALLBACK_&&      callback,
                   const char*      busy_message,
                   Body&&           body)
{
  auto shared_callback = std::make_shared<CALLBACK_>(
      std::move(callback));  // store it once so both paths can use it
  if (!executor.submit(
          [shared_callback, task = std::forward<Body>(body)]() mutable
          {
            task(std::move(*shared_callback));
          }))  // false means the pool is full
  {
    (*shared_callback)(api::v2::Http::Status::SERVICE_UNAVAILABLE,
                       busy_message);  // rejected: answer 503 here and never
                                       // run body
  }
}
