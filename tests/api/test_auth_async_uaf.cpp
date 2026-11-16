/**
 * @file test_auth_async_uaf.cpp
 * @brief Checks that the asynchronous authentication filter still calls the
 * next handler in the chain.
 * @details The test installs a fake gatekeeper that takes the filter's
 * coroutine (async) branch and then drives a request through the filter.
 * Because the callback may run slightly after doFilter returns, the test waits
 * on thread-safe flags rather than for a fixed time. The central assertion is
 * that the "continue the chain" callback really fires.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is a regression test for a use-after-free bug: the filter used to keep a
 * pointer to a local login-data object across a coroutine pause, so a resumed
 * callback read memory that had already been freed. If this test disappeared,
 * that memory-safety bug could return and crash the server under real
 * asynchronous authentication load — the kind of bug that is hard to reproduce
 * by hand.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Coroutines (functions that pause and resume):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::atomic (values safe to use from several threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - Lambdas (small inline functions):
 * https://en.cppreference.com/w/cpp/language/lambda
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Regression test for the async auth filter use-after-free. The filter used to
// capture a pointer to a stack-local login-data object across a coroutine
// suspension; once the request handler returned, the resumed callback read
// freed memory. This drives the asynchronous code path and checks the chain
// callback still fires.
// Drogon's request object, used as input to the filter.
#include <drogon/HttpRequest.h>
// Drogon's response object, passed to the callbacks.
#include <drogon/HttpResponse.h>
// HTTP constants such as status codes and header names.
#include <drogon/HttpTypes.h>
// Drogon's coroutine support, used by the fake's Task return type.
#include <drogon/utils/coroutine.h>

// std::atomic flags shared between the coroutine and the test thread.
#include <atomic>
#include <catch2/catch_test_macros.hpp>  // Catch2's TEST_CASE and CHECK macros.
#include <memory>  // std::make_shared, used to create the fake gatekeeper.
#include <string>  // std::string for the header value.
#include <thread>  // std::this_thread::sleep_for, used while polling the flags.

#include "api/v2/filters/auth.hpp"  // Auth, the filter under test.
// GateKeeper, the class the fake derives from.
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"  // Store, used to swap in the fake gatekeeper.

using AuthFilter =
    api::v2::Filters::Auth;  // Shorter name for the filter type under test.

// Anonymous namespace keeps the fake private to this file.
namespace {
  /** @brief Test seam that forces the asynchronous authentication branch.
   *
   * Overriding only isAuthenticationValidAsync makes the filter take its
   * coroutine path without a database. It also mutates the login-data object
   * the way a successful validation would (filling in the client identity),
   * so the resumed callback observes realistic state. */
  class FakeGateKeeper
      // Inherits the real interface but
      // replaces one method.
      : public api::v2::GateKeeper {
   public:
    drogon::Task<bool> isAuthenticationValidAsync(
        std::optional<api::v2::Types::ClientLoginData>& cd, std::string& /*m*/)
        override  // Coroutine that pretends validation succeeded.
    {
      cd->clientId = 1000;         // Fill in a client id like a real successful
                                   // check would.
      cd->group    = "providers";  // Fill in a group so the resumed callback
                                   // sees realistic data.
      co_return true;              // Report success from the coroutine.
    }
  };
}  // namespace

// Tags let CI select this test by area.
TEST_CASE("Auth filter async path invokes the chain callback",
          "[api][auth][async]") {
  auto fake = std::make_shared<FakeGateKeeper>();  // The fake gatekeeper, kept
                                                   // alive for the whole test.
  auto orig =
      Store::getObject<api::v2::GateKeeper>();  // Save the real object so it
                                                // can be restored.
  Store::replaceObject<api::v2::GateKeeper>(
      fake);  // Install the fake before the filter looks it up.

  auto request =
      drogon::HttpRequest::newHttpRequest();  // Create a fresh request to
                                              // feed the filter.
  request->addHeader("Authorization",
                     "Bearer sometoken");  // Add the header that makes the
                                           // filter try to authenticate.

  AuthFilter        filter;              // The filter instance under test.
  std::atomic<bool> chainCalled{false};  // Thread-safe flag: set true when
                                         // the "continue" callback runs.
  std::atomic<int>  cbCount{
      0};  // Thread-safe counter: how many times the response callback ran.
  filter.doFilter(  // Start filtering; callbacks may run later from the
                    // coroutine.
      request,      // The request being authenticated.
      [&cbCount](const drogon::HttpResponsePtr&) {
        ++cbCount;
      },  // Rejection callback: just count the call.
      [&chainCalled] {
        chainCalled = true;
      });  // Chain callback: signal that the filter let the request through.

  // The chain callback is delivered from the coroutine, so it may arrive
  // after doFilter returns. Poll briefly with atomics instead of sleeping a
  // fixed amount; the 1 ms x 100 bound keeps a regression from hanging.
  for (int i = 0; i < 100 && !chainCalled.load() && cbCount.load() == 0;
       // Stop as soon as a callback fires, otherwise cap the wait at
       // ~100 ms.
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(
        1));  // Give the coroutine a millisecond to make progress.
  }

  Store::replaceObject<api::v2::GateKeeper>(
      orig);  // Restore the real gatekeeper so other tests are unaffected.

  CHECK(chainCalled.load());  // Prove the async path called the chain
                              // callback — the bug made this false.
}
