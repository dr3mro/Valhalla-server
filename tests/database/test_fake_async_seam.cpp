/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_fake_async_seam.cpp
 * @brief Catch2 unit tests and fixtures for fake async seam. Checks the fake DB
 * controller's coroutine path without a real database or Drogon app.
 * @details When DB_ASYNC is enabled, controllers call the ...Coro() methods
 * that return drogon::Task and are resumed with co_await. A helper runs one
 * such coroutine body to completion on a private event-loop thread, then the
 * tests prove the fake answers from the same result fields as the synchronous
 * path while counting a coro_call. A second test proves a "busy" fake throws
 * DatabaseBusy out of co_await, so async callers see the same retryable error
 * as synchronous ones.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects the async database seam in FakeDatabaseController
 * (support/fake_db.hpp) and, through it, every controller path that uses a
 * ...Coro() method when DB_ASYNC is on. Without these tests, async code could
 * only be exercised against a live PostgreSQL server and the full Drogon
 * application, making CI slow and fragile. Regressions where the async branch
 * read different data, skipped a write, or swallowed DatabaseBusy would
 * otherwise go unnoticed.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++20 coroutines and the co_await expression:
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::promise / std::future for signalling between threads:
 * https://en.cppreference.com/w/cpp/thread/future
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The FakeDatabaseController coroutine seam: DB_ASYNC=true paths must be
// testable hermetically, without a real PostgreSQL connection or the Drogon
// application framework.
// Drogon's Task / AsyncTask types that make the coroutine seam work.
#include <drogon/utils/coroutine.h>
// trantor's EventLoop, the engine that drives and resumes the coroutines.
#include <trantor/net/EventLoop.h>

// std::atomic<bool>: a completion flag shared by two threads.
#include <atomic>
// Provides TEST_CASE, CHECK and REQUIRE.
#include <catch2/catch_test_macros.hpp>
// DatabaseBusy, the retryable exception the busy fake throws.
#include <database/databasebusy.hpp>
// std::promise / std::future, used to signal that the loop is ready.
#include <future>
// jsoncons::json, used to build the fake query result.
#include <jsoncons/json.hpp>
// std::make_shared, to create the fake with shared ownership.
#include <memory>
// std::optional, the return type wrapped inside the coroutine result.
#include <optional>
#include <string>  // std::string, for the sample SQL text passed to the fake.
#include <thread>  // std::thread, to host the private event loop.

// FakeDatabaseController: the hermetic DB replacement under test.
#include "support/fake_db.hpp"

namespace
{
  /**
   * @brief Runs one `drogon::Task<void>` body to completion on a private
   * event loop thread.
   * @details The loop must be created on the thread that runs it, the
   * launcher is a fire-and-forget `AsyncTask`, and the helper joins once the
   * body signals done.
   */
  template <typename F>
  void runOnLoop(F&& body)  // Runs the supplied coroutine body on a private
                            // loop and does not return until it finishes.
  {
    // trantor::EventLoop must be constructed on the thread that runs it.
    trantor::EventLoop* loop =
        nullptr;  // Pointer filled in by the worker thread so the caller
                  // can reach its loop.
    std::promise<void> running;  // Promise used to signal "the loop now
                                 // exists and is ready for work".
    std::thread        thread(
        [&loop, &running]
        {
          trantor::EventLoop loopThread;  // The loop is constructed on this
                                          // very thread, as trantor requires.
          loop = &loopThread;   // Publish its address so the main thread
                                // can queue work onto it.
          running.set_value();  // Tell the main thread the loop is ready.
          loopThread.loop();    // Run the event loop; this blocks until
                                // quit() is called.
        });
    running.get_future().wait();  // Block until the worker signals
                                  // readiness, so using 'loop' is safe.

    std::atomic<bool> done{false};  // Flag the coroutine sets when it is done;
                                    // atomic because two threads touch it.
    // The launcher must be a drogon::AsyncTask (fire-and-forget); the
    // awaited bodies return drogon::Task, which is awaitable.
    loop->queueInLoop(
        [loop, &done, &body]() -> drogon::AsyncTask
        {
          co_await body();  // Suspend here until the test's coroutine
                            // body completes.
          done = true;      // Signal completion to the thread that called
                            // runOnLoop.
          loop->quit();     // Stop the event loop so the worker thread can
                            // exit.
        });
    while (!done.load())  // Poll until the coroutine signals it has finished.
    {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(1));  // Sleep briefly each pass instead of
                                          // burning CPU while waiting.
    }
    thread.join();  // Wait for the worker thread to exit before returning
                    // to the caller.
  }
}  // namespace

// The coroutine override reads the same result field as its sync counterpart
// and counts a coro_call but no write: this proves the async seam was actually
// used.
TEST_CASE(
    "FakeDatabaseController coroutines answer from the sync result fields",
    "[database][asyncdb][fake]")  // Registers the test that the async read
                                  // method uses the shared fake result.
{
  auto fake = std::make_shared<
      FakeDatabaseController>();  // Create the fake with shared ownership so
                                  // the coroutine can safely capture it.
  fake->read_result.emplace(jsoncons::json::parse(
      R"({"id":7,"active":true})"));  // Pre-load the row the fake read should
                                      // return; emplace constructs the
                                      // optional in place.

  runOnLoop(
      [&]() -> drogon::Task<void>
      {
        auto result = co_await fake->executeReadQueryCoro(
            "SELECT 1");  // Await the async read; co_await suspends until
                          // the Task produces its value.
        REQUIRE(result.has_value());  // Prove the fake actually returned a
                                      // row before we inspect it.
        CHECK(result->at("id").as<uint64_t>() ==
              7);  // Prove the returned JSON carries the exact id we seeded.
      });
  CHECK(fake->coro_calls == 1);   // Prove exactly one coroutine method ran,
                                  // i.e. the async seam was used.
  CHECK(fake->query_calls == 0);  // nothing was written -- a read path must
                                  // not run any write-capable query
}

// A busy fake throws DatabaseBusy out of co_await, so callers on the async path
// see the same retryable error as on the synchronous path.
TEST_CASE("FakeDatabaseController coroutines propagate DatabaseBusy when busy",
          "[database][asyncdb][fake]")  // Registers the test for async error
                                        // propagation.
{
  auto fake  = std::make_shared<FakeDatabaseController>();  // Create the fake
                                                            // controller with
                                                            // shared ownership.
  fake->busy = true;  // Switch it into the busy state so its methods throw
                      // DatabaseBusy.

  bool threw = false;  // Flag set by the catch block to record that the
                       // exception arrived.
  runOnLoop(
      [&]() -> drogon::Task<void>
      {
        try
        {
          (void)(co_await fake->getPermissionsCoro(
              "SELECT 1"));  // Await a method that should throw; the cast
                             // silences "unused result".
        }
        catch (const DatabaseBusy&)
        {
          threw = true;  // Record that the async path surfaced the same
                         // retryable error as the sync path.
        }
      });
  CHECK(threw);  // Prove the exception really propagated out of co_await into
                 // our catch.
}
