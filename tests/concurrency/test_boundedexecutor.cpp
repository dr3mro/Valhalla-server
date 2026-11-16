/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_boundedexecutor.cpp
 * @brief Checks the bounded worker pool: inline mode, worker identity, queue
 * limits, work stealing and shutdown.
 * @details Tests pin workers with a promise-based gate so the queue fills in a
 * controlled way, then assert that extra work is refused, that a throwing task
 * does not kill a worker, and that queued work is drained exactly once on
 * shutdown. A stress case with many clients checks that no task is left
 * stranded in a queue — a regression for a missed wake-up in the work-stealing
 * logic.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This pool runs database-related work. If it could strand tasks, drop work, or
 * fail to shut down, requests could hang forever or be processed twice. These
 * tests protect the concurrency core that keeps the server responsive and free
 * of deadlocks under load.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - std::promise / std::future (passing results between threads):
 * https://en.cppreference.com/w/cpp/thread/promise
 * - std::atomic (values safe across threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - std::thread (running work on another thread):
 * https://en.cppreference.com/w/cpp/thread/thread
 * - RAII (cleanup in the destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <atomic>  // Thread-safe counters used to observe task completion.
// Catch2's TEST_CASE, CHECK and REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
#include <chrono>      // Time literals and sleeps for the polling loops.
#include <functional>  // Callable support used by the executor.
#include <future>      // std::promise and std::future for cross-thread signals.
#include <stdexcept>   // std::runtime_error, thrown by a test task.
#include <string>      // std::string for error text and messages.
#include <thread>      // std::this_thread::sleep_for and std::thread::id.
#include <utility>     // Utility support for move-only values.
#include <vector>      // std::vector holding the stress-test threads.

// BoundedExecutor, the pool under test.
#include "concurrency/boundedexecutor.hpp"
// DbExecutor and runOnDbExecutor, also exercised here.
#include "database/dbexecutor.hpp"
// CALLBACK_, the callback type used by runOnDbExecutor.
#include "utils/global/callback.hpp"
// Http::Status constants used for response codes.
#include "utils/global/http.hpp"

using namespace std::chrono_literals;  // Lets us write 5s / 10ms instead of
                                       // verbose chrono durations.

namespace  // Anonymous namespace keeps the helper private to this file.
{
  // Lets a blocked task go when the test scope ends, even if a REQUIRE threw,
  // so the worker can be joined.
  struct ReleaseOnExit  // RAII guard that unblocks a task and shuts down the
                        // executor at scope exit.
  {
    std::promise<void>& promise;  // The promise a blocked task waits on.
    bool released = false;        // Remembers whether we already signalled it.
    BoundedExecutor* executor =
        nullptr;  // joined before the objects the blocked task uses go away
    void now()    // Release the blocked task exactly once.
    {
      if (!released)  // Avoid calling set_value twice.
      {
        released = true;      // Mark this guard as spent.
        promise.set_value();  // Wake the waiting task.
      }
    }
    ~ReleaseOnExit()  // Runs even when a REQUIRE throws, so no worker stays
                      // blocked.
    {
      now();                    // Release the task first.
      if (executor != nullptr)  // Only shut down if one was supplied.
      {
        executor->shutdown();  // Drain and join before local objects
                               // are destroyed.
      }
    }
  };
}  // namespace

TEST_CASE("BoundedExecutor with zero workers runs the task inline",
    "[concurrency][executor]")  // Inline mode.
{
  BoundedExecutor executor(0, 4, "test");  // Zero workers, queue size four.
  CHECK(executor.inlineMode());            // Report inline mode.
  CHECK(executor.workerCount() == 0);      // No worker threads were created.

  std::thread::id ran_on;  // Will hold the id of the thread that ran the task.
  REQUIRE(executor.submit(
      [&]
      {
        ran_on = std::this_thread::get_id();
      }));  // Submit a task that records its thread.
  CHECK(ran_on == std::this_thread::get_id());  // It ran on the calling thread,
                                                // as inline mode promises.
}

// onWorkerThread must identify only this executor's workers, not any thread
// pool: a worker of a different executor is deliberately not recognized.
TEST_CASE(
    "BoundedExecutor runs tasks on its own threads and knows which thread is a "
    "worker",
    "[concurrency][executor]")  // Worker identity.
{
  BoundedExecutor executor(2, 8, "test");  // Two workers.
  CHECK(executor.workerCount() == 2);      // Both workers were created.
  CHECK_FALSE(
      executor.onWorkerThread());  // The test thread itself is not a worker.

  std::promise<std::pair<std::thread::id, bool>>
       done;  // Delivers the worker's thread id and its self-check.
  auto future = done.get_future();  // Future the test waits on.
  REQUIRE(executor.submit(
      [&]
      {
        done.set_value({std::this_thread::get_id(), executor.onWorkerThread()});
      }));  // Ask a worker to report where it is.

  REQUIRE(future.wait_for(5s) ==
          std::future_status::ready);  // The task must finish in time.
  const auto [thread_id, on_worker] =
      future.get();  // Read the reported values.
  CHECK(thread_id !=
        std::this_thread::get_id());  // The task ran on a different thread.
  CHECK(on_worker);  // That thread correctly identifies as one of this
                     // executor's workers.

  BoundedExecutor    other(1, 1, "other");  // A second, independent executor.
  std::promise<bool> other_says;  // Delivers whether `other`'s worker is seen
                                  // as `executor`'s worker.
  auto other_future = other_says.get_future();  // Future for that answer.
  REQUIRE(other.submit(
      [&]
      {
        other_says.set_value(executor.onWorkerThread());
      }));  // Ask the other pool's worker to check.
  REQUIRE(other_future.wait_for(5s) ==
          std::future_status::ready);  // Wait for the answer.
  CHECK_FALSE(other_future.get());     // a worker of another executor is not a
                                       // worker of this one
}

TEST_CASE("BoundedExecutor refuses work when the queue is full",
    "[concurrency][executor]")  // Queue limit enforcement.
{
  std::promise<void>
       started_promise;  // Signalled when the blocking task starts.
  auto started = started_promise.get_future();  // Future for that signal.
  std::promise<void> release_promise;           // Used to unblock the task.
  auto               release =
      release_promise.get_future().share();  // Shared future the task waits on.
  std::atomic<int> ran{0};  // Counts how many tasks actually ran.

  {
    BoundedExecutor executor(1, 1, "test");  // One worker, queue size one.
    ReleaseOnExit   gate{release_promise, false,
        &executor};  // Guard that always releases and shuts down.

    REQUIRE(executor.submit(  // Occupy the only worker with a blocking task.
        [&]
        {
          started_promise.set_value();  // Tell the test we have started.
          release.wait();               // Block until released.
          ++ran;                        // Count this task when it finishes.
        }));
    REQUIRE(started.wait_for(5s) ==
            std::future_status::ready);  // the only worker is busy now

    CHECK(executor.submit([&] { ++ran; }));        // fills the queue (size 1)
    CHECK_FALSE(executor.submit([&] { ++ran; }));  // refused
    CHECK(
        executor.pending() == 1);  // Exactly one task is waiting in the queue.

    gate.now();  // Release the blocked task.
  }  // the destructor finishes the queued task and joins
  CHECK(ran == 2);  // Both accepted tasks ran exactly once.
}

// A throwing task must not kill the worker: the pool stays usable for work
// submitted afterwards.
TEST_CASE("BoundedExecutor survives a task that throws",
    "[concurrency][executor]")  // Exception resilience.
{
  BoundedExecutor executor(1, 4, "test");  // One worker.
  REQUIRE(executor.submit(
      [] { throw std::runtime_error("boom"); }));  // Submit a task that throws.

  std::promise<void> done;  // Signalled by the follow-up task.
  auto               future = done.get_future();  // Future the test waits on.
  REQUIRE(executor.submit([&]
      { done.set_value(); }));  // Submit a normal task after the throwing one.
  CHECK(future.wait_for(5s) ==
        std::future_status::ready);  // It still ran: the worker survived.
}

TEST_CASE(
    "runOnDbExecutor answers 503 when the queue is full and never runs the "
    "body",
    "[concurrency][executor]")  // Load shedding through runOnDbExecutor.
{
  std::promise<void>
       started_promise;  // Signalled when the blocking task starts.
  auto started = started_promise.get_future();  // Future for that signal.
  std::promise<void> release_promise;           // Used to unblock the task.
  auto               release =
      release_promise.get_future().share();  // Shared future the task waits on.

  DbExecutor    executor(1, 1);  // One worker, queue size one.
  ReleaseOnExit gate{release_promise, false,
      &executor};  // Guard that releases and shuts down.

  REQUIRE(executor.submit(  // Occupy the only worker.
      [&]
      {
        started_promise.set_value();  // Signal that we are busy.
        release.wait();               // Block until released.
      }));
  REQUIRE(started.wait_for(5s) ==
          std::future_status::ready);  // Wait for the worker to be busy.
  REQUIRE(executor.submit([] {}));     // fills the queue

  int         code = 0;          // Will hold the response status code.
  std::string text;              // Will hold the response message.
  bool        body_ran = false;  // Tracks whether the work body was executed.
  runOnDbExecutor(
      executor,  // Ask the helper to run our body.
      [&](int                c,
          const std::string& t)  // Callback that receives the status and text.
      {
        code = c;  // Save the status code.
        text = t;  // Save the message.
      },
      [&](CALLBACK_&&)
      {
        body_ran = true;
      });  // Body: would run the real work; here it only records.

  CHECK(code == static_cast<int>(
                    // Saturated queue maps to 503.
                    api::v2::Http::Status::SERVICE_UNAVAILABLE));
  CHECK_FALSE(text.empty());  // The client gets an explanatory message.
  CHECK_FALSE(body_ran);      // The body was not run — no work was accepted.
  gate.now();                 // Release the blocked worker.
}

TEST_CASE("runOnDbExecutor hands the callback to the body when it is accepted",
    "[concurrency][executor]")  // Accepted work path.
{
  DbExecutor executor(0, 4);  // inline

  int        code = 0;  // Will hold the status code reported by the body.
  runOnDbExecutor(
      executor,  // Ask the helper to run our body.
      [&](int c, const std::string&)
      { code = c; },  // Save just the status code.
      [](CALLBACK_&& cb)
      {
        std::move(cb)(api::v2::Http::Status::OK, "ok");
      });  // Body immediately answers 200.
  CHECK(code ==
        static_cast<int>(api::v2::Http::Status::OK));  // The body's status
                                                       // reached the callback.
}

// Work-stealing semantics: a blocked worker must not strand queued work while
// another worker is idle. Pin one worker with the gate, then fan out tasks.
TEST_CASE(
    "BoundedExecutor: a task does not wait behind a slow one while another "
    "worker is free",
    "[concurrency][executor]")  // Work stealing.
{
  BoundedExecutor    executor(2, 64, "test");  // Two workers, generous queue.

  std::atomic<int>   done{0};          // Counts tasks that finished.
  std::promise<void> started_promise;  // Signalled by the blocking task.
  auto started = started_promise.get_future();  // Future for that signal.
  std::promise<void> release_promise;           // Used to unblock the task.
  auto               release =
      release_promise.get_future().share();  // Shared future the task waits on.
  ReleaseOnExit gate{release_promise, false,
      &executor};  // Guard that releases and shuts down.

  REQUIRE(executor.submit(  // Pin the first worker.
      [&]
      {
        started_promise.set_value();  // Signal that we are busy.
        release.wait();               // Block until released.
      }));
  REQUIRE(started.wait_for(5s) ==
          std::future_status::ready);  // Wait until it is definitely blocked.

  // The first worker is stuck; every later task must still finish, whichever
  // worker round-robin picks first.
  for (int i = 0; i < 20; ++i)  // Fan out twenty tasks.
  {
    REQUIRE(executor.submit(
        [&] { ++done; }));  // Each task increments the completion count.
  }
  for (int waited = 0; waited < 500 && done < 20;
      ++waited)  // Poll until all finish, with a bounded wait.
  {
    std::this_thread::sleep_for(
        10ms);  // Give the workers a little time between checks.
  }
  CHECK(done == 20);  // All tasks finished despite one blocked worker.
  gate.now();         // Release the blocked worker.
}

// The capacity is a single shared bound (not per-worker), and shutdown drains
// every accepted task exactly once with none left pending.
TEST_CASE(
    "BoundedExecutor: the queue limit is shared by all workers and every task "
    "runs once",
    "[concurrency][executor]")  // Shared limit and shutdown drain.
{
  BoundedExecutor executor(
      4, 8, "test");  // Four workers, shared queue of eight.

  std::atomic<int>   started{0};       // Counts tasks that have begun.
  std::atomic<int>   ran{0};           // Counts tasks that have completed.
  std::promise<void> release_promise;  // Used to unblock the pinned tasks.
  auto               release =
      release_promise.get_future().share();  // Shared future they wait on.
  ReleaseOnExit gate{release_promise, false,
      &executor};  // Guard that releases and shuts down.

  for (int i = 0; i < 4; ++i)  // keep every worker busy
  {
    REQUIRE(executor.submit(  // Submit a blocking task for each worker.
        [&]
        {
          ++started;       // Mark that this task has begun.
          release.wait();  // Block until released.
          ++ran;           // Mark completion.
        }));
  }
  for (int waited = 0; waited < 500 && started < 4;
      ++waited)  // Poll until all four workers are busy.
  {
    std::this_thread::sleep_for(10ms);  // Small pause between checks.
  }
  REQUIRE(started ==
          4);  // All workers are occupied before we test the queue limit.

  int accepted = 0;             // Counts how many extra tasks are accepted.
  for (int i = 0; i < 20; ++i)  // Try to submit twenty more tasks.
  {
    if (executor.submit([&] { ++ran; }))  // If it was accepted...
    {
      ++accepted;  // ...count it.
    }
  }
  CHECK(accepted == 8);            // Only eight fit in the shared queue.
  CHECK(executor.pending() == 8);  // And all eight are still pending.
  gate.now();                      // Release the pinned workers.
  executor.shutdown();             // Drain the queue.
  CHECK(ran == 4 + accepted);      // Every accepted task ran exactly once.
  CHECK(executor.pending() == 0);  // Nothing is left waiting.
}

TEST_CASE(
    "BoundedExecutor: many closed-loop clients never leave a task sitting in a "
    "queue",
    "[concurrency][executor]")  // Missed-wake-up regression.
{
  // Regression: a worker whose wake-up found its queue already emptied by a
  // thief went back to sleep without being marked idle, so later tasks queued
  // behind it and waited until something else woke it.
  BoundedExecutor executor(
      4, 256, "test");  // Four workers, large shared queue.

  constexpr int clients =
      16;  // Number of client threads simulating concurrent callers.
  constexpr int            rounds = 1500;  // Requests per client.
  std::atomic<int>         finished{0};    // Counts requests that completed.

  std::vector<std::thread> threads;  // Holds all client threads.
  for (int c = 0; c < clients; ++c)  // Launch each client.
  {
    threads.emplace_back(  // Create a thread in place.
        [&]
        {
          for (int r = 0; r < rounds; ++r)  // Each client sends many requests.
          {
            std::promise<void>
                 done;  // Signalled when this request's task runs.
            auto future = done.get_future();  // Future the client waits on.
            if (!executor.submit([&done]
                    { done.set_value(); }))  // Submit the task; if refused...
            {
              continue;  // ...skip it and try the next round (queue
                         // momentarily full).
            }
            if (future.wait_for(10s) !=
                std::future_status::ready)  // Wait for the task to run.
            {
              return;  // stuck: the count below fails
            }
            ++finished;  // Count a completed request.
          }
        });
  }
  for (auto& t : threads)  // Wait for every client to finish.
  {
    t.join();  // Joining also keeps the vector alive until all threads end.
  }
  CHECK(finished.load() >
        clients * rounds /
            2);  // a few may be refused when the queue is momentarily full
  CHECK(executor.pending() == 0);  // No task was left stranded in the queue.
}
