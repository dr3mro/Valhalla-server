/**
 * @file boundedexecutor.cpp
 * @brief Implementation of BoundedExecutor: per-worker queues, work stealing
 * and shutdown.
 * @details This file defines the worker pool's behaviour. The constructor
 * creates all worker objects before starting any threads (so every worker can
 * safely look at every queue), then each thread runs a loop that takes tasks,
 * runs them, and sleeps when there is nothing to do. submit() picks a worker
 * without ever waiting for space, and shutdown() stops new work, drains what is
 * queued and joins the threads.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where slow, blocking jobs (database calls, password hashing) actually
 * get executed off Drogon's IO threads. It is called through
 * BoundedExecutor::submit() and runOnExecutor() from the database and
 * gatekeeper code. Because it refuses work when full and runs tasks inline with
 * zero workers, it also controls how the server behaves under load and how the
 * unit tests stay deterministic. Without it, slow work would block the web
 * server or grow an unbounded backlog of queued requests.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - thread_local: a variable with one separate copy per running thread.
 * https://en.cppreference.com/w/cpp/language/storage_duration
 * - Condition variables: a way for a thread to sleep until another thread wakes
 * it with new work. https://en.cppreference.com/w/cpp/thread/condition_variable
 * - Work stealing: an idle worker takes jobs from a busier worker's queue.
 * https://en.wikipedia.org/wiki/Work_stealing
 * - Atomic memory order: rules for when one thread's writes become visible to
 * another. https://en.cppreference.com/w/cpp/atomic/memory_order
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// the declarations implemented below
#include "boundedexecutor.hpp"

// fmt::format: builds the one-line statistics log message
#include <fmt/format.h>

#include <algorithm>  // std::max: clamp queue_max to at least 1
// steady_clock / duration_cast: timestamps and wait-time measurement
#include <atomic>
#include <chrono>
#include <cstddef>     // std::size_t: unsigned counts and indices
#include <cstdint>     // std::uint64_t: the statistics counters
#include <exception>   // std::exception: base class for catchable C++ errors
#include <functional>  // std::function: the type of a queued task
// std::numeric_limits: supplies the "largest possible size" sentinel
#include <limits>
// std::lock_guard / std::unique_lock / std::try_to_lock: lock management
#include <memory>
#include <mutex>
#include <string>   // std::string: pool name and log messages
#include <thread>   // std::thread / std::this_thread::yield: the worker threads
#include <utility>  // std::move / std::forward: move values without copying

// Message::InfoMessage / CriticalMessage: log helpers
#include "utils/message/message.hpp"

namespace {
  // Set while a thread is running tasks for an executor; lets
  // onWorkerThread() answer without a lookup table. thread_local gives each
  // thread its own marker; nullptr means "this thread is not running an
  // executor task".
  thread_local const BoundedExecutor* currentExecutor =  // NOLINT
      nullptr;
}  // namespace

/**
 * Allocates the workers, starts their threads and waits until each reports
 * itself idle.
 *
 * queue_max is clamped to at least 1. All Worker objects are created before any
 * thread starts, because a worker may steal from every other queue. Starting
 * threads only after that avoids a race where a worker sees a
 * not-yet-constructed peer. The final spin (with yield) removes the
 * startup race with an immediate submit(). workers: Number of worker threads; 0
 * gives inline mode. queue_max: Shared queue capacity, clamped to at least 1.
 * name: Log label for this pool.
 */
BoundedExecutor::BoundedExecutor(std::size_t workers,  // NOLINT
                                 std::size_t queue_max,
                                 std::string name)
    : queue_max_(std::max<std::size_t>(queue_max,
                                       1)),
      name_(std::move(name)) {
  workers_.reserve(workers);  // reserve once so push_back cannot reallocate
                              // and move the Worker objects
  for (std::size_t i = 0; i < workers; ++i) {
    // create one Worker record per requested thread
    // allocate each Worker; no thread is started yet
    workers_.push_back(std::make_unique<Worker>());
  }
  // Start the threads only now: a worker looks at every queue, so all of them
  // must exist first.
  for (std::size_t i = 0; i < workers; ++i) {
    workers_.at(i)->thread = std::thread([this, i] {
      // launch the worker loop, capturing this pool and its index
      run(i);
    });
  }
  // Wait until every worker is ready (idle) before returning, so callers that
  // submit immediately after construction do not race with thread startup and
  // trigger stealing.
  for (std::size_t i = 0; i < workers; ++i) {
    // spin until this worker says it is waiting for work
    while (!workers_.at(i)->idle.load(std::memory_order_acquire)) {
      // politely let the OS run the worker instead of burning CPU in a tight
      // loop
      std::this_thread::yield();
    }
  }
}

/**
 * @brief Calls shutdown() so queued work is drained and threads joined before
 * members are destroyed.
 */
BoundedExecutor::~BoundedExecutor() {
  shutdown();
}  // virtual destructor: derived classes are cleaned up as well

/**
 * @brief Stops accepting work, drains queued tasks and joins the workers.
 *
 * @details The stopping flag is published with release semantics; the
 * empty-lock/notify pair then wakes a worker that is either waiting or about to
 * wait, and each worker checks the flag under its own mutex before sleeping.
 * Join is skipped for the calling thread so a task may shut down its own pool.
 * Statistics are logged only when there is something to report, and the
 * counters are cleared so a second call does not log them again.
 * ("Joining" a thread means waiting here until that thread's code has
 * finished.)
 */
void BoundedExecutor::shutdown() {
  stopping_.store(true, std::memory_order_release);
  // publish "no new work" before waking anyone
  // wake every worker so it can notice the flag
  for (auto& worker : workers_) {
    {
      // a sleeping worker either sees the flag or is
      // already waiting
      std::scoped_lock lock(worker->mutex);
    }
    // the short lock above closes the race with a
    // worker going to sleep
    worker->cv.notify_all();
  }
  for (auto& worker : workers_) {
    // never try to join ourselves
    if (worker->thread.joinable() &&
        worker->thread.get_id() != std::this_thread::get_id()) {
      worker->thread.join();  // wait until that worker has finished
                              // draining its queue
    }
  }
  // take a snapshot of the counters before clearing them
  const Stats state = stats();

  // only log when something actually happened
  if (state.submitted > 0 || state.rejected > 0) {
    // Log one summary line built from the counters captured above.
    Message::InfoMessage(
        fmt::format("{}: {} tasks, {} rejected, deepest queue {}, average "
                    "wait {:.1f} us.",
                    name_,
                    state.submitted,
                    state.rejected,
                    state.max_queue,
                    state.avg_wait_us));
    submitted_ = 0;  // report once
    rejected_  = 0;  // clear the counter so a second shutdown() does not log
                     // the same numbers
  }
}

/**
 * @brief Samples the tuning counters with relaxed atomic loads.
 *
 * @return Stats snapshot; avg_wait_us is total wait time divided by waited
 * tasks, in microseconds, or 0.0 before any task has been taken by a worker.
 * @note Values are approximate while the pool is running; they are exact after
 * shutdown().
 */

BoundedExecutor::Stats BoundedExecutor::stats() const {
  // how many tasks went into the total wait time
  const auto waited =  // NOLINT
      waited_tasks_.load(std::memory_order_relaxed);

  // The last value is total nanoseconds divided by the sample count, then
  // converted to microseconds.
  return {.submitted   = submitted_.load(std::memory_order_relaxed),
          .rejected    = rejected_.load(std::memory_order_relaxed),
          .max_queue   = max_queue_.load(std::memory_order_relaxed),
          .avg_wait_us = waited == 0 ? 0.0
                                     : static_cast<double>(wait_ns_.load(
                                           std::memory_order_relaxed)) /
                                           static_cast<double>(waited) /
                                           1000.0};  // NOLINT
}

/**
 * Enqueues a task without blocking, preferring an idle worker and enforcing a
 * shared queue limit.
 *
 * Inline mode runs the task immediately on the caller. Otherwise the shared
 * queued_ counter is reserved with acq_rel semantics and released if the pool
 * turns out to be full or stopping. The target worker is chosen from a
 * round-robin start: the first idle one, else the shortest queue. The stopping
 * flag is checked again while holding that worker's mutex, matching the
 * worker's own exit check. Only a worker that was idle is notified; a busy
 * worker rechecks its queue when it finishes a task. task: Nullary callable to
 * execute (takes no arguments). Returns true if the task ran inline or was
 * queued; false if it was rejected.
 */
bool BoundedExecutor::submit(std::function<void()> task) {
  // no workers: run on the calling thread instead of queueing
  if (inlineMode()) {
    task();       // do the work right now
    return true;  // inline execution always counts as accepted
  }

  // the pool is shutting down
  if (stopping_.load(std::memory_order_acquire)) {
    // count the refusal for diagnostics
    rejected_.fetch_add(1, std::memory_order_relaxed);
    // caller should answer 503 (Service Unavailable)
    return false;
  }

  // Reserve a place in the shared limit first; give it back if the pool turns
  // out to be stopping.
  const std::size_t depth =  // atomically claim one slot and read the new total
      queued_.fetch_add(1, std::memory_order_acq_rel) + 1;
  // are we over the shared limit
  if (depth > queue_max_) {
    queued_.fetch_sub(1,
                      std::memory_order_acq_rel);  // give the unused slot back
    rejected_.fetch_add(1, std::memory_order_relaxed);  // count the rejection
    return false;  // refuse quickly instead of waiting for space
  }

  // From a round-robin start: the first idle worker; with none idle, the one
  // with the shortest queue (the start worker wins ties).
  const std::size_t numWorkers =  // how many workers exist
      workers_.size();

  const std::size_t start =  // rotate the starting point on each call
      next_.fetch_add(1, std::memory_order_relaxed) % numWorkers;

  std::size_t target = start;  // default pick if nobody is idle

  // "infinity", so the first real depth wins
  std::size_t shortest = std::numeric_limits<std::size_t>::max();

  // scan every worker once
  for (std::size_t i = 0; i < numWorkers; ++i) {
    const std::size_t candidate =
        (start + i) % numWorkers;  // wrap around the list
    // found a sleeping worker
    if (workers_.at(candidate)->idle.load(std::memory_order_acquire)) {
      target = candidate;  // give it the job
      break;               // stop scanning
    }

    const std::size_t depth = workers_.at(candidate)->depth.load(
        std::memory_order_relaxed);  // otherwise read its queue length
    // shortest queue seen so far
    if (depth < shortest) {
      shortest = depth;      // remember the new best
      target   = candidate;  // and which worker owns it
    }
  }

  Worker& worker = *workers_.at(target);  // the chosen worker
  // whether a wake-up signal is needed
  bool wake = false;
  {
    std::scoped_lock lock(
        worker.mutex);  // protect the queue while we add to it
    // checked under the lock: the worker's exit check is under it too

    if (stopping_.load(std::memory_order_acquire)) {
      queued_.fetch_sub(1, std::memory_order_acq_rel);  // undo our earlier
                                                        // reservation
      rejected_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    worker.queue.push_back({
        .task      = std::move(task),
        .queued_at = std::chrono::steady_clock::now(),
    });  // store the job together
         // with its arrival time
    worker.depth.store(worker.queue.size(),
                       std::memory_order_relaxed);  // keep the lock-free length
                                                    // estimate current
    // An idle worker is claimed (flag cleared) so the next submit() picks
    // another one instead of queueing behind this task. A busy worker finds
    // its task itself; it needs no wake-up (system call).
    wake = worker.idle
               .exchange(false,
                         std::memory_order_acq_rel);  // claim the idle flag;
                                                      // exchange returns its
                                                      // old value
  }
  submitted_.fetch_add(1,
                       std::memory_order_relaxed);  // count the accepted task
  // a new high-water mark
  if (depth > max_queue_.load(std::memory_order_relaxed)) {
    // remember it
    max_queue_.store(depth, std::memory_order_relaxed);
  }
  // only a sleeping worker needs a wake-up
  if (wake) {
    worker.cv.notify_one();  // system call to wake it
  }
  return true;  // tell the caller the job is queued
}

/**
 * @brief Reports whether the calling thread is currently running tasks for this
 * executor.
 * @return true when the thread_local marker equals this executor.
 */
bool BoundedExecutor::onWorkerThread() const {
  return currentExecutor == this;
}  // did this thread's marker get set to us?

/**
 * Takes a task from this worker's queue, or steals one from a peer if
 * necessary. Tries the worker's own queue under its mutex first. On a miss it
 * scans peers using try_lock, so it never waits for a peer that may be busy
 * with a slow task. Each successful take decrements the shared queued_ counter
 * with acq_rel semantics. self: Index of the calling worker. item: Receives the
 * task and its queueing timestamp. Returns true if a task was taken; false if
 * all queues were empty or locked.
 */
bool BoundedExecutor::take(std::size_t self,
                           Item&       item) {
  Worker& own =  // our own worker record
      *workers_.at(self);

  std::scoped_lock lock(  // protect our queue while we look at it
      own.mutex);         // we have work of our own

  if (!own.queue.empty()) {
    item = std::move(own.queue.front());  // take the oldest task
    own.queue.pop_front();                // remove it from the queue
    own.depth
        .store(own.queue.size(),
               std::memory_order_relaxed);  // update the lock-free estimate
    queued_.fetch_sub(1,
                      std::memory_order_acq_rel);  // one fewer task waiting
                                                   // in total
    return true;                                   // success
  }

  // Nothing of our own: help a worker that is stuck on a slow task. try_lock
  // keeps this from ever waiting on someone.

  const std::size_t numWorkers = workers_.size();  // total number of workers
  for (std::size_t i = 1; i < numWorkers; ++i) {
    // start at 1 so we do not bother re-checking ourselves

    Worker& other =
        *workers_.at((self + i) % numWorkers);  // next worker in a ring

    std::unique_lock<std::mutex>
        lock(other.mutex,
             std::try_to_lock);  // never block; skip this peer if it is busy
    // we got the lock and it has work

    if (lock.owns_lock() && !other.queue.empty()) {
      item = std::move(other.queue.front());  // steal its oldest task
      other.queue.pop_front();                // remove it from that queue
      other.depth.store(other.queue.size(), std::memory_order_relaxed);
      queued_.fetch_sub(1, std::memory_order_acq_rel);
      return true;  // stolen successfully
    }
  }
  return false;  // nobody had any work to give
}

/**
 * Accumulates a task's queue wait time and the number of sampled tasks.
 *
 * Uses a monotonic steady_clock timestamp and relaxed atomics: these counters
 * are tuning gauges, not synchronization points. "Monotonic" means the clock
 * never jumps backwards, so subtracting two timestamps always gives a sensible
 * elapsed time. item: Task whose queued_at timestamp is subtracted from now.
 */
void BoundedExecutor::account(const Item& item) {
  // Add "now minus queued time" (converted to nano sec) to the running total.
  wait_ns_.fetch_add(static_cast<std::uint64_t>(
                         std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - item.queued_at)
                             .count()),
                     std::memory_order_relaxed);
  waited_tasks_
      .fetch_add(1,
                 std::memory_order_relaxed);  // one more sample for the average
}

/**
 * Worker loop: take tasks, run them, and sleep only when every queue is empty.
 *
 * Marks the thread with the thread_local executor pointer, then loops taking
 * and running tasks. A task that throws is logged and swallowed so one
 * misbehaving task cannot end the worker thread. When no work is available the
 * worker re-arms its idle flag on every pass before waiting, because a submit()
 * may have claimed it while a thief took the task, leaving a wake-up with an
 * empty queue; sleeping with idle cleared would hide the worker from every
 * later submit(). The loop exits only when stopping is set and the worker's own
 * queue is empty, at which point no new task can arrive. self: Index of this
 * worker.
 */
void BoundedExecutor::run(std::size_t self) {
  currentExecutor = this;  // mark this thread as one of our workers
  Worker& own     = *workers_.at(self);  // our own worker record
  // keep running until a return inside the loop
  for (;;) {
    Item item;  // empty slot to receive the next task
    // try our queue, then stealing; false means nothing to
    // do
    if (!take(self, item)) {
      std::unique_lock<std::mutex> lock(
          own.mutex);  // hold our lock while deciding whether to sleep
      // double-check after the steal attempt
      if (own.queue.empty()) {
        // shutting down and no work left
        if (stopping_.load(std::memory_order_acquire)) {
          return;  // stopping, and nothing is left in our queue (no
                   // submit can add one any more)
        }
        // idle is set again on every pass: a submit() that claimed us
        // may have lost its task to a thief, and then the wake-up finds
        // an empty queue; going back to sleep with idle cleared would
        // hide us from every later submit().
        // sleep while no work and no shutdown
        while (!stopping_.load(std::memory_order_acquire) &&
               own.queue.empty()) {
          own.idle.store(true,
                         std::memory_order_release);  // advertise that we are
                                                      // available for work
          own.cv.wait(lock);  // sleep; wait releases the lock and
                              // reacquires it on wake-up
        }
        own.idle
            .store(false,
                   std::memory_order_release);  // awake again: no longer idle
      }
      continue;  // look again: a task arrived, or we are stopping
    }

    account(item);  // record how long the task waited in the queue

    try {
      // actually run the job
      // a normal C++ exception escaped the task
      item.task();
    } catch (const std::exception& e) {
      // A task answers its own request; this only keeps a stray exception
      // from ending the thread.
      // log it and keep the worker alive
      // any other, unknown exception
      Message::CriticalMessage(name_ + " task failed: " + e.what());
    } catch (...) {
      Message::CriticalMessage(name_ +
                               // log it and continue
                               " task failed with an unknown exception.");
    }
  }
}
