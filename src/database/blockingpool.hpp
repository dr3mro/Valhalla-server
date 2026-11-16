/**
 * @file blockingpool.hpp
 * @brief Thread-safe bounded pool of reusable resources with idle iteration.
 * @details This header defines BlockingPool<T>, a header-only container that
 * holds a fixed set of reusable items as shared_ptrs. release() adds an item
 * and wakes a waiting acquirer; acquire() waits up to a timeout for an item and
 * returns nullptr if none appears; idle() reports the idle count; forEachIdle()
 * visits every idle item under the lock. A mutex protects the deque and a
 * condition variable handles the waiting.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the generic engine underneath DatabaseConnectionPool. It knows
 * nothing about databases, which makes it reusable and easy to unit test with
 * plain objects. It provides the three properties the server needs: connections
 * are leased one caller at a time, callers wait rather than fail immediately
 * when all are busy, and a bounded wait can be reported as a timeout. Without
 * it the pool would have to reimplement tricky condition-variable logic.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Condition variable (wait until a resource is available):
 * https://en.cppreference.com/w/cpp/thread/condition_variable
 * - std::mutex and std::lock_guard (mutual exclusion / automatic locking):
 * https://en.cppreference.com/w/cpp/thread/lock_guard
 * - std::unique_lock (a lock that can be waited on):
 * https://en.cppreference.com/w/cpp/thread/unique_lock
 * - std::deque (a double-ended queue):
 * https://en.cppreference.com/w/cpp/container/deque
 * - Predicate form of wait_for (also guards against spurious wakeups):
 * https://en.cppreference.com/w/cpp/thread/condition_variable/wait_for
 * - RAII (the lock guard releases the mutex at scope exit):
 * https://en.cppreference.com/w/cpp/language/raii
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: define this template at most once per
              // translation unit.

#include <chrono>  // std::chrono::milliseconds is the acquire timeout type.
// std::condition_variable lets acquirers sleep until an item is released.
#include <condition_variable>
#include <cstddef>  // std::size_t is the idle-count type.
// std::deque stores the idle items; pop_front/push_back are cheap.
#include <deque>
// std::shared_ptr is the stored item type; std::move transfers it without
// copying.
#include <memory>
#include <mutex>  // std::mutex and the lock guards provide thread safety.
// std::move and std::forward move items and callbacks efficiently.
#include <utility>

// A fixed set of reusable items handed out one caller at a time. acquire()
// waits up to a timeout and returns nullptr when nothing became free.
// Header-only and independent of the database so it can be unit tested with
// plain objects.
/**
 * @class BlockingPool
 * @brief Thread-safe fixed set of reusable items leased one caller at a time.
 * @details Items are added with release() and leased with acquire(), which
 * waits up to a timeout and returns `nullptr` if nothing becomes free. The pool
 * is independent of the database so it can be unit tested with plain objects.
 * Used by DatabaseConnectionPool to hand out Database connections;
 * DatabaseHanndler models the matching RAII lease.
 *
 *          Thread-safety: a single mutex protects the deque, a condition
 * variable wakes waiting acquirers, and all methods may be called concurrently.
 * @tparam T Type of the pooled items (held via `std::shared_ptr`).
 */
// T is the resource type; items are owned through
// std::shared_ptr<T>.
// The mutex + condition variable
// make it safe for many threads to
// use at once.
template <typename T>
class BlockingPool {
 public:
  BlockingPool() = default;  // An empty deque needs no special setup.
  BlockingPool(const BlockingPool&) =
      delete;  // Copying a live pool of shared items would duplicate leases.
  BlockingPool(BlockingPool&&) = delete;  // Moving would leave waiters holding
                                          // a stale mutex/condition variable.
  BlockingPool& operator=(const BlockingPool&) =
      delete;  // Copy-assignment would violate single ownership of the queue.
  BlockingPool& operator=(BlockingPool&&) =
      delete;  // Move-assignment would violate single ownership of the queue.
  ~BlockingPool() = default;  // The deque's shared_ptrs release any remaining
                              // items automatically.

  // Adds a new item, or gives one back after use.
  /**
   * @brief Adds an item to the idle set and wakes one waiting acquirer.
   * @param[in,out] item Item to add; null pointers are ignored.
   * @details The notification happens after the lock is released, so a woken
   * acquirer can take the lock immediately.
   */
  // Takes by value so a moved-in item
  // can be stored directly.
  void release(std::shared_ptr<T> item) {
    // A null item is not a usable lease; ignore it silently.
    if (item == nullptr) {
      return;  // Nothing to add and no reason to wake anyone.
    }
    {
      std::lock_guard<std::mutex> lock(
          mutex_);  // Lock the deque briefly; the guard unlocks at the
                    // end of this block.
      items_.push_back(
          std::move(item));  // Put the item at the back of the idle queue
                             // without copying the shared_ptr.
    }
    cv_.notify_one();  // Wake exactly one waiter now that an item is
                       // available (outside the lock to avoid a
                       // wake-then-block).
  }

  /**
   * @brief Leases an idle item, waiting at most `timeout` for one to be
   * released.
   * @param[in] timeout Maximum time to wait.
   * @return The leased item, or `nullptr` if the timeout elapsed (the
   * predicate form of `wait_for` also handles spurious wakeups).
   * @post On success the returned item is no longer in the idle set; the
   * caller must eventually release it (see DatabaseHanndler).
   */
  [[nodiscard]] std::shared_ptr<T> acquire(
      std::chrono::milliseconds
          // [[nodiscard]] warns callers that dropping the returned
          // item would strand it.
          timeout) {
    std::unique_lock<std::mutex> lock(
        mutex_);  // A unique_lock can be released while waiting and
                  // reacquired when woken.
    if (!cv_.wait_for(lock, timeout, [this] {
          // Sleep until an item exists or the timeout expires;
          // the predicate also defeats spurious wakeups.
          return !items_.empty();
        })) {
      return nullptr;  // Timed out: the caller can report exhaustion
                       // (DatabaseBusy in the server).
    }
    std::shared_ptr<T> item =
        std::move(items_.front());  // Take the oldest idle item (FIFO) by
                                    // moving it out.
    items_.pop_front();  // Remove it from the idle queue so no other caller
                         // can lease it.
    return item;         // Hand the exclusive lease to the caller.
  }

  /**
   * @brief Returns the number of currently idle items.
   * @return Idle count, read under the pool lock.
   */
  // A snapshot gauge used in logs and startup progress messages.
  [[nodiscard]] std::size_t idle() const {
    std::lock_guard<std::mutex> lock(
        mutex_);           // Lock so the size cannot change while it is read.
    return items_.size();  // Number of items currently available to lease.
  }

  // Runs func on every idle item, then finish(), all while holding the pool
  // lock (used to reconnect idle connections and wait for them, so none is
  // handed out half way).
  /**
   * @brief Visits every idle item with `func`, then calls `finish`, under the
   * pool lock.
   * @details Holding the lock across both callbacks prevents acquire() from
   * handing out an item while it is being operated on (e.g. reconnected) or
   * before the follow-up work in `finish` has completed.
   * @tparam Func Callable taking `std::shared_ptr<T>&`.
   * @tparam Finish Callable invoked once after all items were visited.
   * @param[in,out] func Per-item operation.
   * @param[in,out] finish Final operation, typically waiting for async work
   * started by func.
   * @note Keeping `finish` inside the lock is intentional; a callback that
   * tries to acquire from the same pool would deadlock.
   */
  // Generic callbacks let callers do any per-item work
  // without changing this class.
  // Both callbacks run while the
  // lock is held, on purpose.
  template <typename Func, typename Finish>
  void forEachIdle(Func&& func, Finish&& finish) {
    std::lock_guard<std::mutex> lock(
        mutex_);  // Hold the lock for the whole visit so nothing idle is
                  // handed out mid-operation.
    for (auto& item :
         // One iteration per idle item currently in the deque.
         items_) {
      func(item);  // Let the caller operate on (e.g. reconnect) this item.
    }
    finish();  // Run the final step (usually waiting for async work) before
               // releasing the lock.
  }

 private:
  mutable std::mutex mutex_;    // Protects items_. `mutable` lets const methods
                                // like idle() lock it.
  std::condition_variable cv_;  // Blocks acquirers until an item is released.
  std::deque<std::shared_ptr<T>>
      items_;  // The idle items themselves, in FIFO order.
};
