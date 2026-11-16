/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_threadsafequeue.cpp
 * @brief Catch2 unit tests for ThreadSafeQueue, a mutex-protected FIFO
 * container.
 * @details ThreadSafeQueue wraps std::queue with a mutex so several threads can
 * push and pop safely. These tests check first-in-first-out order, non-blocking
 * empty behaviour, peeking with front, clearing, in-place emplace, rvalue
 * moves, and accurate size tracking. A producer/consumer test stress-tests the
 * locking under load.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Producers (worker threads, network callbacks) and consumers often run at the
 * same time. This queue lets them hand work over without each call site writing
 * its own locking. A broken FIFO would process items out of order, a missing
 * lock could corrupt the queue under load, and a blocking pop would hang
 * callers. These tests pin all of that down.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertion macros (CHECK, REQUIRE, CHECK_FALSE):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::mutex and std::lock_guard:
 * https://en.cppreference.com/w/cpp/thread/lock_guard
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - std::thread and join: https://en.cppreference.com/w/cpp/thread/thread/join
 * - Perfect forwarding (std::forward):
 * https://en.cppreference.com/w/cpp/utility/forward
 * - std::sort: https://en.cppreference.com/w/cpp/algorithm/sort
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Brings in TEST_CASE, CHECK, REQUIRE and CHECK_FALSE.
#include <catch2/catch_test_macros.hpp>
#include <optional>  // std::optional, the return type of pop and front.
#include <string>    // std::string, the element type in several tests.
#include <thread>    // std::thread, used in the producer/consumer test.
#include <vector>    // std::vector holds the threads and the collected values.

// The queue template under test.
#include "utils/threadsafequeue/threadsafequeue.hpp"

// Items must come out in the same order they went in (first in, first out).
TEST_CASE("ThreadSafeQueue is FIFO", "[utils][queue]")
{
  ThreadSafeQueue<int> queue;  // A thread-safe queue holding integers.

  queue.push(1);  // Add 1 at the back.
  queue.push(2);  // Add 2 behind it.
  queue.push(3);  // Add 3 at the back.

  REQUIRE(queue.size() ==
          3);  // All three inserts must be counted before we start removing.
  CHECK(queue.pop() ==
        std::optional<int>(1));  // The first pop returns the oldest item.
  CHECK(queue.pop() ==
        std::optional<int>(2));  // The second pop returns the middle item.
  CHECK(queue.pop() ==
        std::optional<int>(3));  // The third pop returns the newest item.
  CHECK(queue.size() == 0);      // The queue is empty again after three pops.
}

// Bug 8 fixed: dead condition_variable (and its #include) removed. pop()
// was always non-blocking (returns nullopt); the CV was never waited on.
TEST_CASE(
    "ThreadSafeQueue pop on an empty queue returns nullopt without blocking",
    "[utils][queue]")
{
  ThreadSafeQueue<std::string> queue;  // An empty queue of strings.

  CHECK(queue.pop() == std::nullopt);  // Popping with nothing inside returns
                                       // "no value" immediately.
  CHECK(queue.empty());  // The failed pop must not change the queue; it is
                         // still empty.
}

// front() peeks at the oldest item without removing it.
TEST_CASE("ThreadSafeQueue front peeks without consuming", "[utils][queue]")
{
  ThreadSafeQueue<int> queue;  // An empty integer queue.

  CHECK(queue.front() ==
        std::nullopt);  // Peeking into an empty queue returns "no value".

  queue.push(7);                                  // Add one item.
  CHECK(queue.front() == std::optional<int>(7));  // The peek sees 7.
  CHECK(
      queue.size() == 1);  // peek did not consume  — the item is still counted.
  CHECK(queue.pop() ==
        std::optional<int>(7));  // A later pop still gets that same item.
}

// clear() must discard everything currently in the queue.
TEST_CASE("ThreadSafeQueue clear empties the queue", "[utils][queue]")
{
  ThreadSafeQueue<int> queue;  // An empty integer queue.

  for (int i = 0; i < 5;
      ++i)  // Add five items; the loop stops once i reaches 5.
  {
    queue.push(i);  // Insert the current value.
  }
  CHECK_FALSE(queue.empty());  // After the loop the queue must not be empty.

  queue.clear();                       // Throw all five items away.
  CHECK(queue.empty());                // The queue now reports empty.
  CHECK(queue.size() == 0);            // Its count is zero.
  CHECK(queue.pop() == std::nullopt);  // Nothing is left to pop.
}

// emplace builds an element directly in the queue from constructor arguments.
TEST_CASE("ThreadSafeQueue emplace constructs in place", "[utils][queue]")
{
  struct Point  // A tiny local type with an x and y coordinate, built from
                // two ints.
  {
    int x;  // Horizontal coordinate.
    int y;  // Vertical coordinate.
  };

  ThreadSafeQueue<Point> queue;  // A queue of Point values.
  queue.emplace(1, 2);           // Construct a Point{1,2} in place at the back.
  queue.emplace(3, 4);           // Construct another Point{3,4} behind it.

  const auto first =
      queue.pop();             // Take the first Point (wrapped in an optional).
  REQUIRE(first.has_value());  // A value must exist; stop if the pop failed.
  CHECK(first->x == 1);  // operator-> reaches the Point inside the optional.
  CHECK(first->y == 2);  // Its second field must be 2.

  const auto second = queue.pop();  // Take the second Point.
  REQUIRE(second.has_value());      // It must exist too.
  CHECK(second->x == 3);            // First field of the second Point.
  CHECK(second->y == 4);            // Second field of the second Point.
}

// Several producers push while one consumer pops; no item may be lost or
// duplicated.
TEST_CASE("ThreadSafeQueue handles concurrent producers and consumers",
    "[utils][queue]")
{
  constexpr int kProducers = 4;  // Number of producer threads.
  constexpr int kItemsPerThread =
      250;  // Items each producer pushes; 4 x 250 = 1000 total.

  ThreadSafeQueue<int>     queue;  // The shared queue all threads use.

  std::vector<std::thread> producers;   // Holds the producer threads.
  for (int t = 0; t < kProducers; ++t)  // Launch each producer.
  {
    producers.emplace_back(  // Create a thread running the lambda below.
        [&queue, t]          // Capture the queue by reference and t by value.
        {
          for (int i = 0; i < kItemsPerThread;
              ++i)  // Each producer pushes its share.
          {
            queue.push(t * kItemsPerThread +
                       i);  // Unique value per item: 0..249, 250..499, etc.
          }
        });
  }

  std::vector<int> consumed;  // Collects everything the consumer pops.
  consumed.reserve(
      kProducers *
      kItemsPerThread);  // Reserve room for all 1000 values up front.

  // Consume while the producers are still running; pop() is non-blocking,
  // so spin until everything has been consumed.
  while (
      consumed.size() <
      static_cast<size_t>(
          kProducers * kItemsPerThread))  // Keep going until we have all
                                          // 1000; the cast matches the types.
  {
    while (auto value = queue.pop())  // Drain all items currently available;
                                      // each pass binds the optional to value.
    {
      consumed.push_back(
          *value);  // *value unwraps the optional to get the integer.
    }
  }

  for (auto& producer : producers)  // Walk the producer threads.
  {
    producer.join();  // Wait for them to finish.
  }

  // Drain whatever is left, then compare as sets.
  while (auto value = queue.pop())  // Pick up any final items pushed after
                                    // the main loop ended.
  {
    consumed.push_back(*value);  // Add them to the collected values.
  }

  std::vector<int> expected;  // Will hold the values we should have received.
  expected.reserve(consumed.size());  // Match the size to avoid reallocations.
  for (int t = 0; t < kProducers;
      ++t)  // Rebuild the same value ranges the producers generated.
  {
    for (int i = 0; i < kItemsPerThread;
        ++i)  // Same inner range as the producer side.
    {
      expected.push_back(t * kItemsPerThread +
                         i);  // Identical formula produces the identical value.
    }
  }
  std::sort(expected.begin(),
      expected.end());  // Sort both lists so arrival order does not matter.
  std::sort(consumed.begin(),
      consumed.end());  // Now a direct element-by-element comparison is
                        // meaningful.

  REQUIRE(consumed.size() ==
          expected.size());     // No items lost (fewer) or duplicated (more).
  CHECK(consumed == expected);  // Every expected value appears exactly once.
}

// push(T&&) moves the object in, so the caller's string is transferred rather
// than copied.
TEST_CASE("ThreadSafeQueue push rvalue moves the object in", "[utils][queue]")
{
  ThreadSafeQueue<std::string> queue;  // A queue of strings.

  std::string s = "moved-string";  // The source string we will hand over.
  queue.push(std::move(
      s));  // std::move picks the rvalue overload; s is left valid but empty.

  CHECK(queue.size() == 1);       // Exactly one element was added.
  const auto val = queue.pop();   // Pop it into an optional.
  REQUIRE(val.has_value());       // A value must be present.
  CHECK(*val == "moved-string");  // The text survived the move intact.
}

// size() and empty() must stay in sync with pushes, pops and clears.
TEST_CASE(
    "ThreadSafeQueue size tracks insertions and removals", "[utils][queue]")
{
  ThreadSafeQueue<int> queue;  // An empty integer queue.

  CHECK(queue.size() == 0);  // A new queue has zero elements.
  queue.push(1);             // Add one.
  CHECK(queue.size() == 1);  // The count becomes one.
  queue.push(2);             // Add another.
  CHECK(queue.size() == 2);  // The count becomes two.
  queue.pop();               // Remove the oldest.
  CHECK(queue.size() == 1);  // The count drops back to one.
  queue.clear();             // Remove everything.
  CHECK(queue.size() == 0);  // The count is zero.
  CHECK(queue.empty());      // empty() agrees with the zero count.
}
