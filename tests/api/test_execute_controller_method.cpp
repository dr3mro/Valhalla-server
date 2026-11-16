/**
 * @file test_execute_controller_method.cpp
 * @brief Checks executeControllerMethod: where controller calls run, how
 * responses are produced, and how load is shed.
 * @details Tests use small "probe" controllers that record the thread they ran
 * on and the request body they saw. Cases cover inline execution, worker-thread
 * execution, a full queue answered with 503 and Retry-After, DatabaseBusy
 * turned into 503, unknown types answered with 400, executor shutdown, and the
 * I/O fast path that avoids the queue when no database access is needed.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * executeControllerMethod is the bridge between an incoming HTTP request and
 * the controller that handles it. Getting the threading, request lifetime or
 * error mapping wrong could cause crashes (using freed request data), hangs, or
 * wrong status codes under load. These tests pin down that contract so the
 * request dispatch layer stays safe and predictable.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - std::promise / std::future (passing a result between threads):
 * https://en.cppreference.com/w/cpp/thread/promise
 * - std::atomic (values safe across threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - std::variant (a value that can hold one of several types):
 * https://en.cppreference.com/w/cpp/utility/variant
 * - Lambdas (small inline functions used as callbacks):
 * https://en.cppreference.com/w/cpp/language/lambda
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// executeControllerMethod runs the controller call on the DbExecutor and
// answers from there.
// The request object passed to controller methods.
#include <drogon/HttpRequest.h>
// The response object the dispatcher produces.
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>  // HTTP status codes such as k200OK.

#include <atomic>  // std::atomic counters for thread-safe observations.
// Catch2's TEST_CASE, CHECK and REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
#include <chrono>      // Time literals used for wait_for timeouts.
#include <cstdint>     // Fixed-width integer types for request attributes.
#include <functional>  // Support for callable objects used as callbacks.
#include <future>      // std::promise and std::future for cross-thread results.
#include <memory>      // std::make_shared for controller objects.
#include <string>      // std::string for bodies and registry keys.
#include <string_view>  // std::string_view for non-owning body/registry views.
#include <thread>  // std::this_thread, used to identify which thread ran code.
#include <unordered_map>  // The controller registry is a hash map.
#include <utility>        // std::move, used when invoking callbacks.
#include <variant>        // std::variant, used as the registry's stored value.

// Declares executeControllerMethod, the function under test.
#include "api/v2/basic/common.hpp"
// DatabaseBusy, the exception a controller can throw.
#include "database/databasebusy.hpp"
// DbExecutor, the executor that runs controller calls.
#include "database/dbexecutor.hpp"
// DbProbe, used by the fast path to decide if a database is needed.
#include "database/dbprobe.hpp"
// CALLBACK_, the callback type used by controllers.
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"  // HTTP status helpers such as Http::Status.
#include "utils/global/requester.hpp"  // Requester, the caller-identity value.

using namespace std::chrono_literals;  // Lets us write timeouts as 5s instead
                                       // of std::chrono::seconds{5}.
using api::v2::Requester;              // Shorthand for the requester type.

// Anonymous namespace keeps all test helpers private to this file.
namespace {
  // A fake controller that records what happened to
  // it.
  struct ProbeController {
    std::thread::id ran_on;              // The id of the thread that ran Do().
    std::string     seen_body;           // The request body Do() received.
    bool            throw_busy = false;  // When true, Do() throws DatabaseBusy.
    bool            ran        = false;  // Whether Do() was called at all.

    void Do(CALLBACK_&& callback,
            const Requester&& /*requester*/,
            std::string_view body)  // Stand-in for a real controller method.
    {
      ran       = true;                        // Mark that this controller ran.
      ran_on    = std::this_thread::get_id();  // Remember the thread we are on.
      seen_body = std::string(body);           // Copy the body so it survives
                                               // after the request is gone.
      // Only throw when the test asked for busy
      // behaviour.
      if (throw_busy) {
        throw DatabaseBusy();  // Simulate a controller that could not
                               // reach the database.
      }
      std::move(
          callback)(api::v2::Http::Status::OK,
                    "{\"ok\":true}");  // Answer with 200 and a small JSON body.
    }
  };

  // Answers from memory when `in_memory`, otherwise reaches for the database
  // the way DatabaseController::executer does.
  // A fake controller used to test the I/O fast
  // path.
  struct FastPathController {
    std::thread::id ran_on;  // Thread id of the Do() call.
    int             runs =
        0;  // How many times Do() was called (fast path may run it twice).

    void Do(
        CALLBACK_&& callback,
        const Requester&& /*requester*/,
        bool in_memory)  // `in_memory` decides whether a database is needed.
    {
      ++runs;                               // Count each invocation.
      ran_on = std::this_thread::get_id();  // Remember which thread ran
                                            // this call.
      // True means no database is required.
      if (in_memory) {
        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            "refused without a query");  // Answer immediately
                                                         // with 400.
        return;  // Stop here; no database probe needed.
      }
      DbProbe::check();  // Ask the probe: this call needs the database,
                         // so fail the fast path.
      std::move(callback)(api::v2::Http::Status::OK,
                          "{\"ok\":true}");  // Answer with 200 if the probe
                                             // allowed it.
    }
  };

  using FastRegistry =
      std::unordered_map<std::string_view,
                         // Registry type for fast-path controllers.
                         std::variant<std::shared_ptr<FastPathController>>>;
  using Registry = std::unordered_map<
      std::string_view,
      std::variant<std::shared_ptr<ProbeController>>>;  // Registry type for
                                                        // probe controllers.

  /** @brief Builds a request carrying the body plus the clientID/clientGroup
   * attributes that executeControllerMethod expects on every request. */
  // Helper that fills in the fields the dispatcher reads.
  drogon::HttpRequestPtr makeRequest(const std::string& body) {
    auto req =
        drogon::HttpRequest::newHttpRequest();  // Create a fresh request.
    req->setBody(body);                         // Put the given body in it.
    req->attributes()->insert("clientID",
                              static_cast<uint64_t>(
                                  7));  // Attach a known client id.
    req->attributes()->insert("clientGroup",
                              std::string(
                                  "users"));  // Attach a known client group.
    return req;                               // Hand the prepared request back.
  }

  // Lets a blocked worker task go when the test scope ends, even if a REQUIRE
  // threw, so the worker can be joined.
  // RAII guard that unblocks a task and shuts down an
  // executor at scope exit.
  struct ReleaseOnExit {
    std::promise<void>& promise;  // The promise a blocked task is waiting on.
    bool released = false;        // Remembers whether we already signalled it.
    BoundedExecutor* executor =
        nullptr;  // joined before the objects the blocked task uses go away
    // Release the blocked task exactly once.
    void now() {
      // Avoid setting the promise twice, which would
      // throw.
      if (!released) {
        released = true;      // Mark this guard as spent.
        promise.set_value();  // Wake the waiting task so it can finish.
      }
    }
    // Runs even if a REQUIRE threw, so no worker is left
    // blocked.
    ~ReleaseOnExit() {
      now();  // Release the task first.
      // Only shut down if an executor was supplied.
      if (executor != nullptr) {
        executor->shutdown();  // Drain and join the executor before
                               // local objects disappear.
      }
    }
  };
}  // namespace

// Zero workers: everything runs on the calling thread.
TEST_CASE("executeControllerMethod answers 200 through an inline executor",
          "[api][executor]") {
  auto controller =
      std::make_shared<ProbeController>();  // The fake controller.
  Registry registry = {
      {"probe", controller}};  // Register it under the key "probe".
  DbExecutor executor(0, 4);   // 0 workers means inline mode.

  drogon::HttpResponsePtr response;  // Will hold the dispatcher's response.
  auto req = makeRequest("hello");   // Build a request with body "hello".
  api::v2::executeControllerMethod(
      executor,
      registry,
      "probe",
      &ProbeController::Do,
      req,
      [&response](const drogon::HttpResponsePtr& r) {
        response = r;
      },
      req->body());  // Dispatch the call and capture the response.

  REQUIRE(response != nullptr);  // A response must have been produced.
  CHECK(response->statusCode() ==
        drogon::k200OK);  // Inline execution yields 200.
  CHECK(controller->seen_body ==
        "hello");  // The controller saw the exact body we sent.
}

TEST_CASE(
    "executeControllerMethod runs the controller on a worker thread and keeps "
    // Worker thread + request lifetime.
    "the request data alive",
    "[api][executor]") {
  auto controller =
      std::make_shared<ProbeController>();        // The fake controller.
  Registry   registry = {{"probe", controller}};  // Register it.
  DbExecutor executor(1, 4);                      // One worker thread.

  std::promise<drogon::HttpResponsePtr>
      done;  // Promise used to deliver the response from the worker.
  // Future the test waits on.
  auto future = done.get_future();
  {
    auto req =
        makeRequest("payload that must outlive the handler");  // Request scoped
                                                               // to this block.
    api::v2::executeControllerMethod(
        executor,
        registry,
        "probe",
        &ProbeController::Do,
        req,
        [&done](const drogon::HttpResponsePtr& r) {
          done.set_value(r);
        },
        req->body());  // Dispatch; the task keeps its own copy of the data.
  }  // the handler's own reference to the request is gone; the task holds its
     // own

  REQUIRE(future.wait_for(5s) ==
          std::future_status::ready);  // The worker must finish within five
                                       // seconds.
  auto response = future.get();        // Retrieve the response.
  CHECK(response->statusCode() == drogon::k200OK);  // The result is 200.
  CHECK(controller->ran_on !=
        std::this_thread::get_id());  // Prove it ran on a different thread.
  CHECK(controller->seen_body ==
        "payload that must outlive the handler");  // The body survived the
                                                   // original request going
                                                   // out of scope.
}

// A saturated executor must shed load with 503 + Retry-After rather than block
// or run the controller. The gate pins the single worker so the queue fills
// deterministically before the call under test.
TEST_CASE(
    "executeControllerMethod answers 503 with Retry-After when the executor "
    // Load shedding.
    "queue is full",
    "[api][executor]") {
  auto controller =
      std::make_shared<ProbeController>();        // The fake controller.
  Registry   registry = {{"probe", controller}};  // Register it.
  DbExecutor executor(1, 1);  // One worker, queue size one — easy to saturate.

  std::promise<void>
       started_promise;  // Signalled when the blocking task starts.
  auto started = started_promise.get_future();  // Future for that signal.
  std::promise<void> release_promise;  // Used to let the blocking task finish.
  auto release = release_promise.get_future()
                     .share();  // Shared so the task can wait on it.
  ReleaseOnExit
      gate{release_promise,
           false,
           &executor};  // Guard that always releases the task at scope exit.

  REQUIRE(executor.submit(  // Occupy the only worker with a task that waits.
      [&] {
        started_promise.set_value();  // Tell the test the worker is busy.
        release.wait();               // Block until the guard releases us.
      }));
  REQUIRE(started.wait_for(5s) ==
          std::future_status::ready);  // Wait until the worker is definitely
                                       // busy.
  REQUIRE(executor.submit([] {
  }));  // the queue (size 1) is full now

  drogon::HttpResponsePtr response;  // Will hold the dispatcher's response.
  auto                    req = makeRequest("x");  // A minimal request.
  api::v2::executeControllerMethod(
      executor,
      registry,
      "probe",
      &ProbeController::Do,
      req,
      [&response](const drogon::HttpResponsePtr& r) {
        response = r;
      },
      req->body());  // Try to dispatch while saturated.

  REQUIRE(response != nullptr);  // A response must exist.
  CHECK(response->statusCode() ==
        drogon::k503ServiceUnavailable);  // Shedding load yields 503.
  CHECK(response->getHeader("Retry-After") ==
        "1");  // The client is told to retry after one second.
  CHECK_FALSE(controller->ran);  // The controller must not have run at all.
  gate.now();  // Release the blocked worker so it can be joined cleanly.
}

TEST_CASE(
    "executeControllerMethod turns DatabaseBusy thrown by the controller into "
    // Busy exception mapping.
    "503",
    "[api][executor]") {
  auto controller =
      std::make_shared<ProbeController>();  // The fake controller.
  controller->throw_busy = true;            // Make it throw DatabaseBusy.
  Registry   registry    = {{"probe", controller}};  // Register it.
  DbExecutor executor(0, 4);                         // Inline executor.

  drogon::HttpResponsePtr response;  // Will hold the dispatcher's response.
  auto                    req = makeRequest("x");  // A minimal request.
  api::v2::executeControllerMethod(
      executor,
      registry,
      "probe",
      &ProbeController::Do,
      req,
      [&response](const drogon::HttpResponsePtr& r) {
        response = r;
      },
      req->body());  // Dispatch the call.

  REQUIRE(response != nullptr);  // A response must exist.
  CHECK(response->statusCode() ==
        drogon::k503ServiceUnavailable);  // The exception is mapped to 503.
}

// Unknown controller key.
TEST_CASE("executeControllerMethod answers 400 for an unknown type",
          "[api][executor]") {
  Registry   registry;        // Deliberately empty registry.
  DbExecutor executor(0, 4);  // Inline executor.

  drogon::HttpResponsePtr response;  // Will hold the dispatcher's response.
  auto                    req = makeRequest("x");  // A minimal request.
  api::v2::executeControllerMethod(
      executor,
      registry,
      "nope",
      &ProbeController::Do,
      req,
      [&response](const drogon::HttpResponsePtr& r) {
        response = r;
      },
      req->body());  // Ask for a key that is not registered.

  REQUIRE(response != nullptr);  // A response must exist.
  CHECK(response->statusCode() ==
        drogon::k400BadRequest);  // Unknown type is a client error (400).
}

TEST_CASE(
    "BoundedExecutor shutdown finishes queued work, refuses new work and "
    // Shutdown behaviour.
    "reports stats",
    "[concurrency][executor]") {
  DbExecutor       executor(1, 8);  // One worker, queue of eight.
  std::atomic<int> ran{0};          // Thread-safe count of completed tasks.
  // Submit five small tasks.
  for (int i = 0; i < 5; ++i) {
    REQUIRE(executor.submit([&] {
      ++ran;
    }));  // Each submission must be accepted.
  }
  executor.shutdown();  // Drain the queue and stop accepting work.
  CHECK(ran == 5);      // All five tasks finished before shutdown returned.
  CHECK_FALSE(executor.submit([&] {
    ++ran;
  }));                  // New work is refused after shutdown.
  executor.shutdown();  // second call is harmless
  CHECK(ran == 5);      // No task ran from the refused submissions.
}

// The I/O fast path handles requests that need no database entirely on the
// calling thread and bypasses the executor queue (stats().submitted stays 0).
// Fast path stays inline.
TEST_CASE(
    "fast path answers a request that needs no database on the calling thread",
    "[api][executor]") {
  auto controller = std::make_shared<FastPathController>();  // The fast-path
                                                             // fake controller.
  FastRegistry registry = {{"fast", controller}};            // Register it.
  DbExecutor   executor(1, 4, true);  // Enable the I/O fast path.
  REQUIRE(executor.ioFastPath());  // Confirm the fast path is actually active.

  drogon::HttpResponsePtr response;  // Will hold the dispatcher's response.
  auto                    req = makeRequest("x");  // A minimal request.
  api::v2::executeControllerMethod(
      executor,
      registry,
      "fast",
      &FastPathController::Do,
      req,
      [&response](const drogon::HttpResponsePtr& r) {
        response = r;
      },
      true);  // `true` tells the controller no database is needed.

  REQUIRE(response != nullptr);  // A response must exist.
  CHECK(response->statusCode() ==
        drogon::k400BadRequest);  // The controller's in-memory answer is 400.
  CHECK(controller->ran_on ==
        std::this_thread::get_id());  // It ran on the calling thread, not a
                                      // worker.
  CHECK(controller->runs == 1);       // It ran exactly once.
  CHECK(executor.stats().submitted ==
        0);  // The executor queue was bypassed entirely.
}

// When the fast-path probe decides the request needs the database, the whole
// controller call is re-run from the beginning on a worker thread (hence the
// doubled run count), not merely resumed after the probe.
TEST_CASE(
    "fast path hands a request that needs the database to the executor and "
    // Fast path falls back to executor.
    "runs it from the start",
    "[api][executor]") {
  auto controller = std::make_shared<FastPathController>();  // The fast-path
                                                             // fake controller.
  FastRegistry registry = {{"fast", controller}};            // Register it.
  DbExecutor   executor(1, 4, true);  // Enable the I/O fast path.

  std::promise<drogon::HttpResponsePtr>
       done;                        // Promise for the response from the worker.
  auto future = done.get_future();  // Future the test waits on.
  auto req    = makeRequest("x");   // Request kept alive across the call.
  api::v2::executeControllerMethod(
      executor,
      registry,
      "fast",
      &FastPathController::Do,
      req,
      [&done](const drogon::HttpResponsePtr& r) {
        done.set_value(r);
      },
      false);  // `false` means a database IS needed.

  REQUIRE(future.wait_for(5s) ==
          std::future_status::ready);  // The worker must finish in time.
  CHECK(future.get()->statusCode() ==
        drogon::k200OK);         // The retried call returns 200.
  executor.shutdown();           // Stop the executor before checking counters.
  CHECK(controller->runs == 2);  // once on the probe, once on the worker (the
                                 // probe is not active there)
  CHECK(controller->ran_on !=
        std::this_thread::get_id());  // The final run was on a worker thread.
}

// Fast path requires at least one worker.
TEST_CASE("fast path is off without workers", "[api][executor]") {
  DbExecutor executor(0, 4, true);     // Fast path requested, but zero workers.
  CHECK_FALSE(executor.ioFastPath());  // It is disabled because there is no
                                       // worker to fall back to.
}
