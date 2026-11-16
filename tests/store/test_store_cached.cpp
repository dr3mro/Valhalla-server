/**
 * @file test_store_cached.cpp
 * @brief Checks that Store's per-thread cache returns the current object after
 * replacements, even from other threads.
 * @details The first test registers a probe object, reads it twice through the
 * cache, replaces it and checks the new object is seen immediately. The second
 * test performs the replacement from a different thread to prove that cache
 * invalidation is global rather than only local to the replacing thread.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The Store holds shared singletons (configuration, gatekeeper, database
 * controller, and more). A per-thread cache speeds up repeated lookups, but if
 * it went stale after a replacement, different parts of the server could use
 * different versions of a component at the same time. These tests guard that
 * coherence, which matters for the test doubles and any runtime
 * reconfiguration.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and assertions:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Thread-local storage (a separate copy of a variable per thread):
 * https://en.cppreference.com/w/cpp/language/storage_duration
 * - std::shared_ptr (shared ownership of an object):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - std::thread (running work on another thread):
 * https://en.cppreference.com/w/cpp/thread/thread
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Catch2's TEST_CASE, CHECK and REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
#include <memory>  // std::make_shared, used to create replacement objects.
// std::thread, used to replace the object from another thread.
#include <thread>

#include "store/store.hpp"  // Store, the registry whose cache is under test.

// Anonymous namespace keeps the probe type private to this file.
namespace {
  /** @brief Neutral registration type used to observe the Store cache. Its
   * content is irrelevant; pointer identity is what the tests compare. */
  // A minimal object; only its address matters for these
  // tests.
  struct CachedProbe {
    int value = 1;  // A field so the object is not empty; its value is
                    // never asserted.
  };
}  // namespace

// cachedObject is a per-thread fast path that must stay coherent with
// replaceObject: repeated reads reuse the cached pointer, but a replacement is
// observed without any explicit per-thread signal.
TEST_CASE(
    "Store::cachedObject returns the registered object and follows "
    // Same-thread cache coherence.
    "replaceObject",
    "[store][cache]") {
  auto original =
      Store::getObject<CachedProbe>();  // Get (and register if needed) the
                                        // current object.
  CHECK(Store::cachedObject<CachedProbe>() ==
        original);  // The cache returns that same object.
  CHECK(Store::cachedObject<CachedProbe>() ==
        original);  // second call uses the thread's own pointer

  auto replacement = std::make_shared<CachedProbe>();  // Create a different
                                                       // object to swap in.
  Store::replaceObject<CachedProbe>(
      replacement);  // Replace the registered object.
  CHECK(Store::cachedObject<CachedProbe>() ==
        replacement);  // the replace is noticed without any other signal

  Store::replaceObject<CachedProbe>(
      original);  // Restore the original so other tests are unaffected.
  CHECK(Store::cachedObject<CachedProbe>() ==
        original);  // The cache follows the restore as well.
}

// The cache invalidation must be cross-thread, not just local: a replacement
// made on another thread has to be seen by this thread's cached lookup.
// Cross-thread cache coherence.
TEST_CASE("Store::cachedObject notices a replace made by another thread",
          "[store][cache]") {
  auto original =
      Store::getObject<CachedProbe>();  // The object this thread will cache.
  auto replacement = std::make_shared<CachedProbe>();  // The object another
                                                       // thread will install.
  REQUIRE(Store::cachedObject<CachedProbe>() ==
          original);  // this thread has cached the original

  std::thread other([&] {
    Store::replaceObject<CachedProbe>(replacement);
  });            // Replace the object from a separate thread.
  other.join();  // Wait for that thread to finish before checking the result.

  CHECK(Store::cachedObject<CachedProbe>() ==
        replacement);  // This thread sees the new object despite having
                       // cached the old one.
  Store::replaceObject<CachedProbe>(
      original);  // Restore the original for other tests.
}
