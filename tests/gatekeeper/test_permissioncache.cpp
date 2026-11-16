/**
 * @file test_permissioncache.cpp
 * @brief Catch2 unit tests and fixtures for permissioncache. Checks the
 * TTL-based cache of permission JSON with generation-based invalidation.
 * @details BasicPermissionCache is instantiated with a manual, process-global
 * clock so TTL expiry is deterministic and never needs real sleeping. The tests
 * prove a miss-then-hit cycle, expiry after the idle TTL, that reading an entry
 * renews its timer, that stale entries are swept on a later put, that
 * invalidateAll drops everything, that a put racing with an invalidation is
 * discarded using a generation counter, and that a TTL of zero disables the
 * cache entirely.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects permissioncache.hpp, the cache that keeps permission lookups from
 * hitting the database on every request. If entries never expired, revoked
 * permissions would linger; if they expired too eagerly, the database would be
 * hammered; a missing generation check could let a stale put reintroduce
 * permissions just after an invalidation. These tests pin down all three
 * properties without depending on wall-clock timing.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::chrono durations (the manual clock stands in for these):
 * https://en.cppreference.com/w/cpp/chrono
 * - std::optional, which get() returns when an entry is present:
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides TEST_CASE and the CHECK / REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
// std::chrono::seconds for the TTL values passed to the cache.
#include <chrono>
// jsoncons::json, the permission data stored in the cache.
#include <jsoncons/json.hpp>

// The production header under test: BasicPermissionCache.
#include "gatekeeper/permissionmanager/permissioncache.hpp"
#include "support/fake_clock.hpp"  // ManualClock, the controllable time source.

namespace {
  /** @brief Cache instantiated with the manual clock so TTLs are
   * deterministic.
   *
   * ManualClock is process-global, so every test resets it first; no real
   * waiting is involved in any expiry assertion below.
   */
  using TestCache =
      BasicPermissionCache<ManualClock>;  // Type alias that binds the cache
                                          // template to the controllable
                                          // clock.
  const jsoncons::json kPerm = jsoncons::json::parse(
      R"({"owner_id":1,"admin_id":2,"staff":{}})");  // One fixed permission
                                                     // document reused by
                                                     // every test.
}  // namespace

// Registers the basic
// store-and-retrieve test.
TEST_CASE("PermissionCache miss then hit", "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Reset the global fake clock so this test starts
                         // from a known time.
  TestCache cache(
      std::chrono::seconds{30});  // Create a cache with a 30-second idle TTL.

  CHECK_FALSE(
      cache.get("q1").has_value());  // Prove an empty cache misses (get
                                     // returns no value) for an unknown key.
  cache.put("q1",
            kPerm,
            cache.generation());  // Store the permission document under "q1"
                                  // with the current generation.
  auto hit = cache.get("q1");     // Read it back.
  REQUIRE(hit.has_value());       // Prove the entry is now present before we
                                  // inspect its contents.
  CHECK(hit->at("owner_id").as<int>() ==
        1);  // Prove the retrieved JSON is the exact document we put in.
}

// Registers the test for TTL expiry.
TEST_CASE("PermissionCache entries expire after the idle TTL",
          "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Start the global fake clock at a known point.
  TestCache cache(std::chrono::seconds{300});  // Five-minute idle TTL.
  cache.put("q1", kPerm, cache.generation());  // Store one entry at time zero.

  ManualClock::advance(std::chrono::seconds{
      299});  // Move to one second before expiry without any real waiting.
  CHECK(cache.get("q1").has_value());  // Prove the entry is still alive just
                                       // before the deadline.
  ManualClock::advance(
      std::chrono::seconds{301});  // Jump well past the 300-second TTL.
  CHECK_FALSE(cache.get("q1").has_value());  // Prove the entry has expired
                                             // and is no longer returned.
}

// Registers the test that reads
// keep an entry alive.
TEST_CASE("PermissionCache use renews the idle timer",
          "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Start the shared clock from zero.
  TestCache cache(std::chrono::seconds{300});  // Five-minute idle TTL.
  cache.put("q1", kPerm, cache.generation());  // Store the entry once.

  // Repeatedly advance by less than the TTL;
  // each read should restart the timer.
  for (int i = 0; i < 5; ++i) {
    ManualClock::advance(std::chrono::seconds{
        200});  // Move 200 s forward, still under the 300 s limit.
    REQUIRE(cache.get("q1").has_value());  // Prove the entry survives and
                                           // is refreshed by this read.
  }
  ManualClock::advance(std::chrono::seconds{
      301});  // Finally jump past the TTL with no intervening read.
  CHECK_FALSE(
      cache.get("q1").has_value());  // Prove the entry now expires, confirming
                                     // reads were what kept it alive earlier.
}

// Registers the test that old entries are
// cleaned up on a later write.
TEST_CASE("PermissionCache sweeps idle entries that are never read again",
          "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Start the clock from a known point.
  TestCache cache(std::chrono::seconds{300});  // Five-minute idle TTL.
  cache.put("idle",
            kPerm,
            cache.generation());  // Add one entry that will be left untouched.

  ManualClock::advance(std::chrono::seconds{
      400});  // Move beyond the TTL so that entry is now stale.
  cache.put("fresh",
            kPerm,
            cache.generation());  // Add a second entry; the put also sweeps
                                  // expired ones.
  CHECK(cache.size() == 1);  // Prove only the fresh entry remains, i.e. the
                             // stale one was swept.
}

// Registers the test for clearing
// the whole cache.
TEST_CASE("PermissionCache invalidateAll drops every entry",
          "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Start the shared clock from zero.
  TestCache cache(std::chrono::seconds{
      30});  // Short TTL, though expiry is not involved in this test.
  cache.put("q1", kPerm, cache.generation());  // Add the first entry.
  cache.put("q2", kPerm, cache.generation());  // Add a second entry.

  cache.invalidateAll();  // Clear the cache, bumping its generation so old
                          // entries cannot return.
  CHECK_FALSE(cache.get("q1").has_value());  // Prove the first entry is gone.
  CHECK_FALSE(
      cache.get("q2").has_value());  // Prove the second entry is gone too.
  CHECK(cache.size() ==
        0);  // Prove the cache is completely empty, not just returning misses.
}

// Registers the test for the
// generation-counter race guard.
TEST_CASE("PermissionCache drops a put that raced with an invalidation",
          "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Start the shared clock from zero.
  TestCache cache(std::chrono::seconds{30});  // Cache under test.

  const auto generation =
      cache.generation();  // reader starts -- a reader captures the current
                           // generation before fetching.
  cache.invalidateAll();   // writer commits meanwhile -- invalidation advances
                           // the generation.
  cache.put("q1",
            kPerm,
            generation);  // reader stores stale data -- it tries to write using
                          // the now-outdated generation.

  CHECK_FALSE(
      cache.get("q1")
          .has_value());  // Prove the stale write was rejected, so freshly
                          // invalidated permissions cannot reappear.
}

// Registers the test for the "turn
// the cache off" configuration.
TEST_CASE("PermissionCache with TTL 0 is disabled",
          "[gatekeeper][permissioncache]") {
  ManualClock::reset();  // Start the shared clock from zero.
  TestCache cache(std::chrono::seconds{
      0});  // A zero TTL is the documented way to disable caching.
  cache.put("q1",
            kPerm,
            cache.generation());  // Attempt to store an entry anyway.
  CHECK_FALSE(cache.enabled());   // Prove the cache reports itself as disabled.
  CHECK_FALSE(cache.get("q1").has_value());  // Prove disabled means no data is
                                             // ever served, even after a put.
}
