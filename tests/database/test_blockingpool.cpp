/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_blockingpool.cpp
 * @brief Catch2 unit tests and fixtures for blockingpool. Checks the bounded,
 * timeout-based item pool used by the synchronous DB path.
 * @details BlockingPool lends items exclusively, waits up to a timeout for one,
 * wakes a blocked caller when an item is released, and returns borrowed items
 * safely even if the caller throws. The tests also cover null releases,
 * forEachIdle (which visits only idle items and runs a finish callback last),
 * and heavy multithreaded use with more threads than items. Plain ints and
 * atomics stand in for real DB connections, so no database is needed.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects blockingpool.hpp, the pool that lends handler items to
 * synchronous request threads. If two threads could obtain the same item, they
 * would share one connection and corrupt each other's queries; if a thrown
 * exception failed to return an item, the pool would slowly leak connections
 * until every request timed out. These tests pin down exclusive hand-out,
 * timeouts, wake-ups, and cleanup so the sync DB path cannot stall or
 * double-use a resource.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE / CHECK_THROWS_AS assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - RAII (the Lease fixture returns its item on scope exit, even during
 * exceptions): https://en.cppreference.com/w/cpp/language/raii
 * - Condition variables (how a waiting acquire is woken by release):
 * https://en.cppreference.com/w/cpp/thread/condition_variable
 * - std::chrono clocks, durations and duration literals:
 * https://en.cppreference.com/w/cpp/chrono
 * - Atomic compare-and-swap (the lock-free high-water-mark update):
 * https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// std::atomic: counters shared safely between threads without a mutex.
#include <atomic>
// Provides TEST_CASE and the CHECK/REQUIRE family of macros.
#include <catch2/catch_test_macros.hpp>
// Steady clock, durations and the ms/s literals used for timeouts.
#include <chrono>
// std::make_shared / std::shared_ptr: the pool stores items by shared pointer.
#include <memory>
// std::runtime_error: the exception thrown by the test's simulated failing
// query.
#include <stdexcept>
// std::thread: the worker threads used in the wake-up and stress tests.
#include <thread>
// std::move: transfers item ownership back to the pool without copying.
#include <utility>
#include <vector>  // std::vector: holds the collection of worker threads.

// The production header under test: BlockingPool.
#include "database/blockingpool.hpp"

// BlockingPool is the bounded, timeout-based item pool used by the synchronous
// DB path. These tests pin down exclusive hand-out, timeout, wake-up, cleanup
// when a caller throws, and safe concurrent use — with plain ints, no database.

using namespace std::chrono_literals;  // Lets us write 10ms / 2s instead of
                                       // spelling out
                                       // std::chrono::milliseconds.

namespace
{
  /**
   * @brief RAII fixture that returns an acquired item on scope exit, even if
   * the user throws.
   * @details Mirrors DatabaseHandler so a failed query cannot leak a pool
   * item.
   */
  struct Lease
  {
    BlockingPool<int>&   pool;  // Reference to the pool this lease belongs
                                // to; the pool must outlive the lease.
    std::shared_ptr<int> item;  // The checked-out item, held by shared
                                // pointer so ownership is explicit.
    Lease(BlockingPool<int>& p, std::chrono::milliseconds timeout)
        : pool(p), item(p.acquire(timeout))
    {
    }  // Acquire at construction; if the wait times out, item ends up null.
    Lease(const Lease&) = delete;  // No copying: two leases would both think
                                   // they own the item and release it twice.
    Lease& operator=(const Lease&) =
        delete;  // No copy-assignment, for the same double-release reason.
    ~Lease()
    {
      pool.release(std::move(item));
    }  // On scope exit, even while an exception unwinds, hand the item
       // back.
  };
}  // namespace

// Two releases give two distinct items; acquiring both empties the idle count.
TEST_CASE("BlockingPool hands out each item once",
    "[database][pool]")  // Registers the test that checks exclusive hand-out.
{
  BlockingPool<int>
      pool;  // An empty pool of int items, standing in for DB connections.
  pool.release(std::make_shared<int>(
      1));  // Add the first item, holding value 1, to the idle list.
  pool.release(std::make_shared<int>(
      2));                  // Add a second, distinct item, holding value 2.
  CHECK(pool.idle() == 2);  // Prove both items are idle and available.

  auto a =
      pool.acquire(10ms);  // Take one item, waiting at most 10 ms; the short
                           // budget fails fast if something is wrong.
  auto b = pool.acquire(
      10ms);  // Take another within 10 ms; a second item is available.
  REQUIRE(a != nullptr);  // Prove the first acquire succeeded before we use
                          // the result.
  REQUIRE(b != nullptr);  // Prove the second acquire succeeded too.
  CHECK(a != b);  // Prove the two callers received DIFFERENT items (no double
                  // hand-out).
  CHECK(pool.idle() ==
        0);  // Prove both items are now checked out, leaving none idle.
}

// The elapsed-time check proves acquire really waits for the timeout instead
// of returning nullptr immediately.
TEST_CASE("BlockingPool::acquire returns nullptr after the timeout",
    "[database][pool]")  // Registers the test for wait-then-give-up behaviour.
{
  BlockingPool<int> pool;  // An empty pool, so no item can ever be acquired.
  const auto        started =
      std::chrono::steady_clock::now();  // Record the start on a monotonic
                                         // clock that never jumps backwards.
  CHECK(
      pool.acquire(50ms) == nullptr);  // Prove an empty pool returns "no item"
                                       // once the 50 ms budget is exhausted.
  CHECK(std::chrono::steady_clock::now() - started >=
        50ms);  // Prove it really waited at least 50 ms, rather than failing
                // instantly.
}

// A waiter blocked in acquire must be woken by a later release, not wait out
// its two-second timeout.
TEST_CASE("BlockingPool::release wakes a waiting acquirer",
    "[database][pool]")  // Registers the test for notify-on-release behaviour.
{
  BlockingPool<int>    pool;  // An empty pool to begin with.
  std::shared_ptr<int> got;   // Shared variable where the worker thread stores
                              // the item it receives.
  std::thread          waiter(
      [&]
      {
        got = pool.acquire(2s);
      });  // Start a thread that blocks in acquire for up to 2 seconds.

  std::this_thread::sleep_for(50ms);  // Give the worker time to actually start
                                      // waiting before we release an item.
  pool.release(std::make_shared<int>(
      7));  // Add an item; this should notify and wake the blocked acquirer.
  waiter.join();  // Wait for the worker to finish so we can safely inspect
                  // 'got'.

  REQUIRE(got != nullptr);  // Prove the waiter received an item (if it had
                            // slept the full timeout it would be null).
  CHECK(*got == 7);         // Prove it received the exact item we released.
}

// A null release is a no-op; it must not inflate the idle count.
TEST_CASE("BlockingPool ignores a null release",
    "[database][pool]")  // Registers the defensive test for a bad release call.
{
  BlockingPool<int> pool;   // An empty pool.
  pool.release(nullptr);    // Try to "return" a null item; a failed acquisition
                            // could produce this.
  CHECK(pool.idle() == 0);  // Prove the pool ignored it rather than counting
                            // a phantom idle item.
}

// The Lease fixture's destructor returns the item even when the body throws.
TEST_CASE("BlockingPool gets the item back when the user throws",
    "[database][pool]")  // Registers the exception-safety test.
{
  BlockingPool<int> pool;                  // Pool with a single item to lend.
  pool.release(std::make_shared<int>(1));  // Put one item in the idle list.

  auto use = [&]
  {
    Lease lease(pool,
        10ms);  // Acquire through the RAII helper; its destructor will
                // release even if we throw.
    REQUIRE(lease.item != nullptr);  // Prove the lease really obtained an item.
    throw std::runtime_error("query failed");  // Simulate a database query
                                               // failing part-way through.
  };
  CHECK_THROWS_AS(use(),
      std::runtime_error);  // Prove the simulated exception really escaped
                            // the lambda.
  CHECK(pool.idle() == 1);  // Prove the destructor returned the item, so the
                            // pool did not leak it.
}

// One item is checked out, so only the other is visited; finish() runs last
// (while the pool lock is still held).
TEST_CASE(
    "BlockingPool::forEachIdle visits only idle items and runs finish last",
    "[database][pool]")  // Registers the test for the inspection helper.
{
  BlockingPool<int> pool;                  // Pool with two items.
  pool.release(std::make_shared<int>(1));  // First idle item.
  pool.release(std::make_shared<int>(2));  // Second idle item.
  auto busy =
      pool.acquire(10ms);  // Check one out so that only one remains idle.

  int  visited = 0;  // Counter for how many idle items the callback sees.
  bool finished =
      false;  // Flag set by the finish callback, used to check its timing.
  pool.forEachIdle([&](std::shared_ptr<int>&) { ++visited; },
      [&]
      {
        finished = (visited == 1);
      });  // Visit each idle item, then run finish; the count proves only the
           // idle one was seen.
  CHECK(visited ==
        1);  // Prove only the idle item was visited, not the checked-out one.
  CHECK(finished);  // Prove finish ran last and observed the final visited
                    // count of 1.
}

// More threads than items: nobody times out, never more than ITEMS are in use
// at once, and every item is back in the pool once all threads finish.
TEST_CASE("BlockingPool survives many threads sharing few items",
    "[database][pool][stress]")  // Registers the multithreaded stress test.
{
  constexpr int THREADS = 16;  // Number of competing worker threads,
                               // deliberately larger than ITEMS.
  constexpr int ITEMS   = 4;   // Number of items in the pool; scarcity forces
                               // threads to wait for one another.

  BlockingPool<std::atomic<int>>
      pool;  // Pool of atomic counters, so using an item needs no extra lock.
  for (int i = 0; i < ITEMS; ++i)  // Loop once per item to seed the pool.
  {
    pool.release(std::make_shared<std::atomic<int>>(
        0));  // Create a counter initialised to 0 and add it to the idle
              // list.
  }

  std::atomic<int> in_use{0};  // How many items are checked out right now;
                               // atomic because many threads update it.
  std::atomic<int> max_in_use{
      0};  // High-water mark of simultaneous checkouts, also atomic.
  std::atomic<int> timeouts{
      0};  // Count of acquires that gave up; should stay zero.

  std::vector<std::thread> threads;  // Holds all workers so we can join them
                                     // before checking results.
  for (int t = 0; t < THREADS; ++t)  // Spawn one worker per thread.
  {
    threads.emplace_back(  // Construct a thread in place with the lambda
                           // below as its body.
        [&]
        {
          const auto until =
              std::chrono::steady_clock::now() +
              300ms;  // Each worker runs for a fixed 300 ms budget.
          while (std::chrono::steady_clock::now() <
                 until)  // Keep going until the deadline; this is the
                         // loop's stop condition.
          {
            auto item =
                pool.acquire(2s);  // Wait up to 2 s for a free item; generous,
                                   // so a timeout means a real bug.
            if (item == nullptr)   // Defensive check: did the wait give up?
            {
              ++timeouts;  // Record the unexpected timeout so the
                           // test fails at the end.
              continue;    // Skip this iteration and try again.
            }
            const int now = ++in_use;  // Atomically bump the in-use count and
                                       // remember our new value.
            int prev = max_in_use.load();  // Read the current high-water mark.
            while (now > prev && !max_in_use.compare_exchange_weak(prev, now))
            {
            }  // Lock-free "max" update: retry the compare-and-swap
               // while our value is larger; weak CAS may fail
               // spuriously, hence the loop.
            ++(*item);  // Actually use the item by incrementing its
                        // counter.
            --in_use;   // We are finished with it, so lower the in-use
                        // count.
            pool.release(std::move(item));  // Return the item to the pool;
                                            // std::move avoids a copy.
          }
        });
  }
  for (auto& th : threads)  // Loop over every worker thread.
  {
    th.join();  // Wait for each to finish so all counters are final before
                // we assert on them.
  }

  CHECK(timeouts == 0);        // Prove no acquire timed out under contention.
  CHECK(max_in_use <= ITEMS);  // Prove the pool never handed out more items
                               // than it actually owns.
  CHECK(pool.idle() ==
        ITEMS);  // Prove every item made it back after all threads finished.
}
