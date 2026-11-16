/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_passwordworker.cpp
 * @brief Catch2 unit tests and fixtures for passwordworker. Checks the bounded
 * worker pool that runs expensive password hashing off the request thread.
 * @details PasswordWorker can run with zero threads (inline mode) or on its own
 * threads with a bounded queue, and it refuses work when that queue is full.
 * The tests cover inline versus threaded execution, queue-full rejection, a
 * concurrency cap measured with a lock-free high-water mark, resilience to a
 * throwing task, and the runOnPasswordWorker adapter that turns an accepted
 * task into a callback and answers 503 when the queue is full. A ReleaseOnExit
 * RAII latch guarantees blocked workers are released even if an assertion
 * throws.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects passwordworker.hpp, the pool that keeps slow Argon2 hashing from
 * blocking Drogon's event loops. If the cap were wrong, too many hashes would
 * run at once and exhaust CPU and memory; if a throwing task killed a worker,
 * logins would stop being processed; if a full queue were not reported as HTTP
 * 503, callers would wait forever for a response that never arrives.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - RAII (the ReleaseOnExit latch always unblocks the worker on scope exit):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::promise / std::future for coordinating threads:
 * https://en.cppreference.com/w/cpp/thread/future
 * - Atomic compare-and-swap, the lock-free "peak" calculation:
 * https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <atomic>  // std::atomic counters shared safely across worker threads.
// Provides TEST_CASE and the CHECK / REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
// Durations and the ms/s literals used for waits and timeouts.
#include <chrono>
#include <functional>  // The callable types used by the callback adapter.
// std::promise / std::future / shared_future, used to block and release
// workers.
#include <future>
// std::runtime_error, the exception thrown by the test's failing task.
#include <stdexcept>
// std::string, used for the response body in the callback test.
#include <string>
#include <thread>  // std::thread, the worker threads managed by PasswordWorker.

// The production header under test: PasswordWorker.
#include "gatekeeper/passwordcrypt/passwordworker.hpp"
// CALLBACK_ type used by the runOnPasswordWorker adapter.
#include "utils/global/callback.hpp"
// api::v2::Http::Status codes (OK, SERVICE_UNAVAILABLE).
#include "utils/global/http.hpp"

using namespace std::chrono_literals;  // Lets us write 2s / 5ms instead of
                                       // spelling out the chrono types.

namespace
{
  /** @brief RAII latch that releases a blocked worker task on scope exit.
   *
   * The destructor runs even when a REQUIRE throws, so the worker's blocking
   * wait is always unblocked and its destructor can join the thread. This
   * matters because the tasks below capture references to stack locals: if a
   * task were still running when those locals went out of scope, the pool's
   * destructor (which joins the worker threads) would leave a window for a
   * use-after-free.
   */
  struct ReleaseOnExit
  {
    std::promise<void>& promise;  // Reference to the promise that, when
                                  // set, releases the blocked worker.
    bool released = false;        // Guard so we only signal once; setting a
                                  // promise twice would throw.
    void now()  // Release the worker now, if it has not already been released.
    {
      if (!released)  // Only act on the first call.
      {
        released = true;      // Mark as released before signalling.
        promise.set_value();  // Fulfil the promise, unblocking the
                              // worker's shared_future::wait().
      }
    }
    ~ReleaseOnExit()
    {
      now();
    }  // On scope exit, always release; this is what makes the guard
       // exception-safe.
  };
}  // namespace

TEST_CASE("PasswordWorker with zero workers runs the task inline",
    "[gatekeeper][passwordworker]")  // Registers the test for the no-thread
                                     // fallback mode.
{
  PasswordWorker worker(0,
      4);  // Construct with zero workers, which selects inline mode; queue
           // size is irrelevant but still supplied.
  CHECK(worker.inlineMode());  // Prove the zero-worker configuration really
                               // enables inline mode.

  std::thread::id ran_on;  // Records which thread executed the submitted task.
  REQUIRE(worker.submit(
      [&]
      {
        ran_on = std::this_thread::get_id();
      }));  // Submit a task that records its thread id; submit returns true
            // on success.
  CHECK(ran_on == std::this_thread::get_id());  // Prove the task ran on the
                                                // calling thread, i.e. inline
                                                // rather than on a worker.
}

TEST_CASE("PasswordWorker runs tasks on its own threads",
    "[gatekeeper][passwordworker]")  // Registers the test for normal threaded
                                     // mode.
{
  PasswordWorker worker(2, 8);       // Two worker threads, queue of eight.
  CHECK_FALSE(worker.inlineMode());  // Prove we are NOT in inline mode when
                                     // workers are configured.

  std::promise<std::thread::id>
       ran_on;  // Promise the task will fulfil with its thread id.
  auto done = ran_on.get_future();  // Future used to wait for that id.
  REQUIRE(worker.submit(
      [&]
      {
        ran_on.set_value(std::this_thread::get_id());
      }));  // Submit the task; it will report whichever worker thread runs
            // it.

  REQUIRE(
      done.wait_for(2s) ==
      std::future_status::ready);  // Wait up to 2 s; proves the task actually
                                   // started instead of being lost.
  CHECK(done.get() !=
        std::this_thread::get_id());  // Prove it ran on a different thread
                                      // from the one calling submit.
}

TEST_CASE("PasswordWorker refuses work when the queue is full",
    "[gatekeeper][passwordworker]")  // Registers the test for bounded-queue
                                     // backpressure.
{
  std::promise<void>
      started_promise;  // Signalled when the first task actually begins.
  std::future<void> started =
      started_promise
          .get_future();  // Lets the test wait for that start signal.
  std::promise<void> release_promise;  // Will release the blocked first task.
  std::shared_future<void> release =
      release_promise.get_future()
          .share();  // shared_future so the waiting lambda can hold a copy.
  std::atomic<int> ran{0};  // Counts how many tasks actually ran; atomic
                            // because workers update it.

  {
    PasswordWorker worker(1,
        1);  // One worker and a queue of exactly one, so we can fill it
             // deterministically.
    ReleaseOnExit  gate{
        release_promise};  // RAII guard that will release the blocked task
                           // even if an assertion throws.

    // Occupies the only worker until released.
    REQUIRE(worker.submit(
        [&]
        {
          started_promise.set_value();  // Tell the test the task has
                                        // started and is about to block.
          release.wait();  // Block until the ReleaseOnExit guard fulfils
                           // the promise.
          ++ran;           // Count this task as done once released.
        }));
    started.wait();  // Wait until we know the worker is busy; this makes
                     // the queue state predictable.

    REQUIRE(
        worker.submit([&] { ++ran; }));  // waits in the queue (1 of 1) -- the
                                         // single queue slot is still free.
    CHECK(worker.pending() ==
          1);  // Prove exactly one task is queued behind the busy worker.
    CHECK_FALSE(worker.submit(
        [&] { ++ran; }));  // queue full -- a third task must be refused
                           // rather than silently dropped or queued.

    gate.now();  // Release the blocked task so it and the queued one can
                 // finish.
  }  // destructor finishes the queued task -- leaving the scope lets the pool
     // join its workers.

  CHECK(ran == 2);  // Prove both accepted tasks completed, confirming the
                    // refused third one never ran.
}

TEST_CASE("PasswordWorker never runs more tasks at once than it has workers",
    "[gatekeeper][passwordworker]")  // Registers the test that the concurrency
                                     // cap is honoured.
{
  constexpr int WORKERS =
      2;  // Maximum number of tasks that should ever run at the same time.
  constexpr int    TASKS = 12;  // Many more tasks than workers, so they must
                                // queue and run in batches.

  std::atomic<int> running{0};  // How many tasks are executing right now.
  std::atomic<int> peak{
      0};  // Highest observed value of 'running' across the run.
  std::atomic<int> done{0};  // How many tasks have finished.
  {
    PasswordWorker worker(WORKERS,
        TASKS);  // Pool with the two workers and a queue big enough for all
                 // tasks.
    for (int i = 0; i < TASKS; ++i)  // Submit every task.
    {
      REQUIRE(worker.submit(
          [&]
          {
            const int now  = ++running;  // Atomically mark ourselves as running
                                         // and get the current count.
            int       prev = peak.load();  // Read the current peak before
                                           // trying to update it.
            // Lock-free "peak = max(peak, now)": retry the CAS only
            // while this thread's count exceeds the observed peak.
            while (now > prev && !peak.compare_exchange_weak(prev, now))
            {
            }  // Compare-and-swap updates the peak; a weak CAS may fail
               // spuriously, so retry while our count is still larger.
            std::this_thread::sleep_for(5ms);  // Stay busy briefly so overlaps
                                               // between tasks are observable.
            --running;  // We are finishing, so lower the running count.
            ++done;     // Record that this task completed.
          }));
    }
  }  // Leaving the scope destroys the pool, which joins its workers after
     // finishing all tasks.
  CHECK(done == TASKS);    // Prove every submitted task ran to completion.
  CHECK(peak <= WORKERS);  // Prove the pool never executed more than two
                           // tasks simultaneously.
}

TEST_CASE("A throwing task does not stop the worker",
    "[gatekeeper][passwordworker]")  // Registers the test that one bad task
                                     // cannot kill the pool.
{
  std::atomic<int> ran{0};  // Counts how many subsequent tasks completed.
  {
    PasswordWorker worker(1, 4);  // Single worker with a queue of four.
    REQUIRE(worker.submit(
        []
        {
          throw std::runtime_error("task failed");
        }));  // Submit a task that throws; the pool must contain the
              // exception.
    REQUIRE(worker.submit(
        [&] { ++ran; }));  // Submit a normal task after the failing one; it
                           // must still be accepted.
  }  // Destructor joins the worker, waiting for both tasks.
  CHECK(ran == 1);  // Prove the worker survived the exception and executed
                    // the following task.
}

TEST_CASE("runOnPasswordWorker hands the callback to the task",
    "[gatekeeper][passwordworker]")  // Registers the test for the adapter that
                                     // bridges worker submission and the HTTP
                                     // callback.
{
  PasswordWorker worker(0,
      4);         // Inline mode makes this test deterministic: the task runs
                  // immediately on this thread.
  int code = -1;  // Receives the status passed to the callback; -1 means "not
                  // called yet".
  std::string body;  // Receives the response body passed to the callback.

  runOnPasswordWorker(
      worker,
      [&](int c, const std::string& b)
      {
        code = c;  // The callback records the status code from the task.
        body = b;  // ...and the response body.
      },
      [](CALLBACK_&& callback)
      {
        callback(api::v2::Http::Status::OK, "done");
      });  // This lambda stands in for the real work; it calls the callback
           // with OK and "done".

  CHECK(code == api::v2::Http::Status::OK);  // Prove the status from the task
                                             // reached the caller's callback.
  CHECK(body ==
        "done");  // Prove the body travelled through the adapter unchanged.
}

TEST_CASE(
    "runOnPasswordWorker answers 503 and skips the task when the queue is full",
    "[gatekeeper][passwordworker]")  // Registers the backpressure test for the
                                     // callback adapter.
{
  std::promise<void>
      started_promise;  // Signalled when the first blocking task starts.
  std::future<void> started =
      started_promise.get_future();          // Used to wait for that start.
  std::promise<void>       release_promise;  // Will release the blocked task.
  std::shared_future<void> release =
      release_promise.get_future()
          .share();  // Shared so the task lambda can wait on it.
  int  code = -1;  // Receives the status the adapter reports; -1 means nothing
                   // reported yet.
  bool body_ran =
      false;  // Set true if the task body is (incorrectly) executed.
  {
    PasswordWorker worker(
        1, 1);  // One worker and a one-slot queue, so both can be filled.
    ReleaseOnExit gate{release_promise};  // RAII guard to release the blocked
                                          // task even if an assertion fails.
    REQUIRE(worker.submit(
        [&]
        {
          started_promise
              .set_value();  // Signal that the worker is now occupied.
          release.wait();    // Block until the guard releases us.
        }));
    started.wait();  // Wait until we know the single worker is busy.
    REQUIRE(worker.submit(
        [] {}));  // fills the queue -- occupy the only queue slot.

    runOnPasswordWorker(
        worker, [&](int c, const std::string&) { code = c; }, [&](CALLBACK_&&)
        { body_ran = true; });  // Try to submit through the adapter while
                                // both worker and queue are full.

    CHECK(code ==
          api::v2::Http::Status::
              SERVICE_UNAVAILABLE);  // Prove the adapter reports 503 instead
                                     // of silently dropping the request.
    CHECK_FALSE(body_ran);  // Prove the task body was skipped, so no work
                            // runs when admission is refused.
    gate.now();  // Release the blocked task so the pool can shut down
                 // cleanly.
  }
}
