/**
 * @file test_memcache.cpp
 * @brief Catch2 unit tests for MemCache, the thread-safe expiring LRU cache.
 * @details MemCache stores values by key, evicts the least recently used entry
 * when full, and drops entries after a time-to-live. These tests cover basic
 * insert/get/remove, LRU eviction with and without a read "touch", concurrent
 * access, move construction, and TTL expiry. A manually advanced clock
 * (ManualClock) makes the TTL tests deterministic so they need no real
 * sleeping.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The cache speeds up repeated access to data that is costly to fetch but
 * rarely changes (for example permissions or database results). If LRU order
 * were wrong, hot data would be thrown away; if TTL expiry broke, stale data
 * could be served indefinitely; if the mutex were missing, concurrent requests
 * could corrupt the store. These tests protect all three behaviours plus the
 * cleaner-thread lifecycle during moves.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertion macros (CHECK, CHECK_FALSE):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::unordered_map:
 * https://en.cppreference.com/w/cpp/container/unordered_map
 * - std::list: https://en.cppreference.com/w/cpp/container/list
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - std::chrono time points:
 * https://en.cppreference.com/w/cpp/chrono/time_point
 * - std::thread and join: https://en.cppreference.com/w/cpp/thread/thread/join
 * - LRU cache replacement policy:
 * https://en.wikipedia.org/wiki/Cache_replacement_policies
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Brings in TEST_CASE, CHECK and CHECK_FALSE.
#include <catch2/catch_test_macros.hpp>
#include <chrono>    // std::chrono::seconds, the unit used for TTL values.
#include <optional>  // std::optional, the type returned by get().
#include <string>    // The cached key and value types here are strings.
#include <thread>    // std::thread, used in the concurrency test.

// ManualClock, the controllable clock used by the TTL tests.
#include "support/fake_clock.hpp"
#include "utils/memcache/memcache.hpp"  // The MemCache template under test.

namespace {
  using StringCache =
      MemCache<std::string>;  // A cache storing strings, measured with the
                              // real steady clock.
  using ManualStrCache = MemCache<std::string,
                                  ManualClock>;  // Same value type, but with a
                                                 // clock the tests control.
}  // namespace

// The most basic contract: insert a value, read it back, and miss on an unknown
// key.
TEST_CASE("MemCache stores and retrieves values", "[utils][memcache]") {
  StringCache cache(10,
                    std::chrono::seconds{
                        100});  // Capacity of 10 entries, default lifetime of
                                // 100 seconds.

  cache.insert("k1",
               "v1");  // Store "v1" under key "k1", using the default TTL.
  CHECK(cache.get("k1") ==
        std::optional<std::string>(
            "v1"));  // A hit returns the value wrapped in an optional.
  CHECK(cache.get("missing") ==
        std::nullopt);  // An absent key returns the empty optional (a miss).
}

// Inserting the same key twice must replace the value, not create a duplicate
// entry.
TEST_CASE("MemCache insert on an existing key updates the value",
          "[utils][memcache]") {
  StringCache cache(10,
                    std::chrono::seconds{
                        100});  // A fresh cache for this scenario.

  cache.insert("k", "first");  // First insert creates the entry.
  cache.insert("k",
               "second");  // Second insert finds the key and overwrites its
                           // value.
  CHECK(cache.get("k") ==
        std::optional<std::string>("second"));  // The latest value wins.
}

// remove deletes an entry and reports whether it actually found one.
TEST_CASE("MemCache removes entries", "[utils][memcache]") {
  StringCache cache(10, std::chrono::seconds{100});  // A fresh cache.

  cache.insert("k", "v");    // Put one entry in.
  CHECK(cache.remove("k"));  // Removing an existing key must report true.
  CHECK(cache.get("k") == std::nullopt);  // ...and the key is now a miss.
  CHECK_FALSE(cache.remove("k"));  // removing again reports failure  — it is
                                   // already gone, so false is correct.
}

// With capacity 2, a read ("touch") changes which entry is considered oldest.
TEST_CASE("MemCache evicts the least recently used entry when full",
          "[utils][memcache]") {
  StringCache cache(2, std::chrono::seconds{100});  // Only two entries fit.

  cache.insert("a", "1");  // First entry; it is initially the most recent.
  cache.insert("b", "2");  // Second entry; "a" is now the least recently used.
  // Touching "a" makes it the most recently used; "b" becomes the LRU.
  CHECK(cache.get("a").has_value());  // has_value() is true because the read
                                      // succeeded and reordered the list.
  cache.insert("c", "3");  // Cache is full, so the new key evicts the LRU entry
                           // ("b").

  CHECK(cache.get("b") ==
        std::nullopt);  // evicted  — "b" was pushed out to make room.
  CHECK(cache.get("a") ==
        std::optional<std::string>(
            "1"));  // "a" survived because the touch protected it.
  CHECK(cache.get("c") == std::optional<std::string>(
                              "3"));  // The newly inserted entry is present.
}

// Without reads, insertion order decides the victim: the oldest entry goes
// first.
TEST_CASE("MemCache LRU eviction without any touch", "[utils][memcache]") {
  StringCache cache(2, std::chrono::seconds{100});  // Capacity of two again.

  cache.insert("a", "1");  // Oldest entry.
  cache.insert("b", "2");  // Middle entry.
  cache.insert("c",
               "3");  // full: evicts the oldest, "a"  — inserting a third key
                      // forces one out.

  CHECK(cache.get("a") ==
        std::nullopt);  // "a" was least recently used, so it is gone.
  CHECK(cache.get("b") ==
        std::optional<std::string>("2"));  // "b" is still cached.
  CHECK(cache.get("c") ==
        std::optional<std::string>("3"));  // The newest entry is present.
}

// A zero default TTL makes every entry expired immediately, which lets us test
// expiry without sleeping.
TEST_CASE("MemCache entries with defaultTTL=0 are already expired on read",
          "[utils][memcache]") {
  // With defaultTTL = 0 an entry expires the moment it is inserted: the
  // expiration time is Clock::now() and get() compares <= now. This makes
  // expiry testable without sleeping on the real clock.
  StringCache cache(10,
                    std::chrono::seconds{
                        0});  // Zero-second lifetime means "expired at once".

  cache.insert("k", "v");  // Insert with no explicit TTL, so the zero default
                           // applies.
  CHECK(cache.get("k") ==
        std::nullopt);  // get() sees expiry <= now and reports a miss.
}

// insert() treats a zero TTL as "use the default" - it cannot express "expire
// now".
TEST_CASE("MemCache insert with an explicit zero ttl uses the default ttl",
          "[utils][memcache]") {
  // insert() cannot express "expire immediately": a ttl of zero is treated
  // as "use defaultTTL_" (documented quirk, tests/README.md).
  StringCache cache(10,
                    std::chrono::seconds{
                        100});  // Default lifetime is 100 seconds.

  cache.insert("k",
               "v",
               std::chrono::seconds{
                   0});  // Explicit zero TTL is replaced by the default.
  CHECK(cache.get("k") ==
        std::optional<std::string>(
            "v"));  // So the entry is still fresh and readable.
}

// Many threads writing and reading at once must not corrupt the cache or lose
// entries.
TEST_CASE("MemCache survives concurrent insert/get traffic",
          "[utils][memcache]") {
  StringCache
      cache(1000,
            std::chrono::seconds{
                100});  // Large enough that no eviction happens in this test.

  std::vector<std::thread>
      threads;  // Holds the worker threads we are about to launch.
  for (int t = 0; t < 4;
       // Four workers; t becomes part of each key so keys stay unique.
       ++t) {
    // Build a thread in place from the lambda below.
    // Capture the cache by reference (shared) and t by value
    // (each thread's own copy).
    threads.emplace_back([&cache, t] {
      // Each worker inserts 100 keys.
      for (int i = 0; i < 100; ++i) {
        const auto key =
            "k" +
            std::to_string(t * 100 +
                           i);  // Unique key such as "k0", "k1" ... "k399".
        cache.insert(key,
                     key);     // Store the key as both the lookup key and the
                               // value.
        (void)cache.get(key);  // Read it back; the (void) cast says
                               // "ignore this result on purpose".
      }
    });  // The closing brace ends the lambda body, ); ends
         // emplace_back.
  }
  // Walk the list of workers.
  for (auto& thread : threads) {
    thread.join();  // Wait for each to finish before checking the final
                    // state.
  }

  // All 400 distinct keys fit (maxSize 1000) and must be readable.
  // Revisit the same key range.
  for (int t = 0; t < 4; ++t) {
    // Same 100 values per worker.
    for (int i = 0; i < 100; ++i) {
      const auto key =
          "k" + std::to_string(t * 100 + i);  // Rebuild the same key string.
      CHECK(cache.get(key) ==
            std::optional<std::string>(
                key));  // Every key must still map to its own value.
    }
  }
}

// Bug 6 fixed: move constructor now stops and joins the source's cleaner
// before moving state, then starts a fresh cleaner on the destination.
TEST_CASE("MemCache move constructor transfers entries to the destination",
          "[utils][memcache]") {
  StringCache src(10,
                  std::chrono::seconds{
                      100});  // Source cache that owns the entries.
  src.insert("a", "1");       // First entry in the source.
  src.insert("b", "2");       // Second entry in the source.

  StringCache dst(std::move(src));  // Move everything into a new cache; src's
                                    // cleaner is stopped first.

  CHECK(dst.get("a") ==
        std::optional<std::string>(
            "1"));  // The destination can read the first transferred entry.
  CHECK(dst.get("b") ==
        std::optional<std::string>("2"));  // ...and the second one too.
}

// Note: The background cleaner's 1s cadence is not asserted on the real
// clock; expiry is covered deterministically via TTL tests above.

// Despite the name, this pins the zero-TTL quirk rather than a real per-insert
// override: TTL=0 means "use the default", so the entry stays readable. A
// genuine custom TTL (a positive value) is covered by the ManualClock cases
// below.
TEST_CASE("MemCache custom TTL per insert takes precedence over default",
          "[utils][memcache]") {
  // Default TTL is 100s; insert one entry with TTL=0 (the special "use
  // default" value).
  StringCache cache(10,
                    std::chrono::seconds{
                        100});  // Default lifetime of 100 seconds.

  cache.insert("short",
               "v",
               std::chrono::seconds{0});  // Zero TTL falls back to the default
                                          // instead of expiring.
  // TTL 0 means "use default" (see quirk note above), so this should still be
  // readable.
  CHECK(cache.get("short") ==
        std::optional<std::string>(
            "v"));  // Confirms the documented zero-TTL behaviour.
}

// Removing two different keys one after another must each behave correctly.
TEST_CASE("MemCache handles multiple removes correctly", "[utils][memcache]") {
  StringCache cache(10, std::chrono::seconds{100});  // A fresh cache.

  cache.insert("a", "1");  // Add the first key.
  cache.insert("b", "2");  // Add the second key.

  CHECK(cache.remove("a"));  // First removal of "a" succeeds.
  CHECK_FALSE(
      cache.remove("a"));  // second remove fails  — "a" is already gone.
  CHECK(cache.get("a") == std::nullopt);  // "a" is no longer retrievable.
  CHECK(cache.get("b") ==
        std::optional<std::string>("2"));  // Removing "a" left "b" untouched.
  CHECK(cache.remove("b"));  // The remaining key can still be removed.
}

// Repeated inserts with the same key keep one entry whose value is the newest.
TEST_CASE("MemCache update resets value but entry remains accessible",
          "[utils][memcache]") {
  StringCache cache(10, std::chrono::seconds{100});  // A fresh cache.

  cache.insert("k", "original");  // First value.
  cache.insert("k", "updated");   // Second value overwrites the first.
  cache.insert("k", "final");     // Third value overwrites the second.

  CHECK(cache.get("k") ==
        std::optional<std::string>("final"));  // Only the last value survives.
}

// ── Deterministic TTL tests using ManualClock ────────────────────────────────
// ManualClock is a static (process-global) clock; reset before each test.
// Advancing the clock by hand avoids real-time sleeps and makes boundaries
// exact.

// At 9999 ms of a 10 s lifetime the entry must still be alive (just before
// expiry).
TEST_CASE("MemCache entry is present just before its TTL elapses",
          "[utils][memcache][ttl]") {
  ManualClock::reset();  // Start the shared fake clock at zero so this test
                         // is independent.
  ManualStrCache cache(10,
                       std::chrono::seconds{10});  // Ten-second lifetime
                                                   // measured on ManualClock.

  cache.insert("k", "v");  // Expiry is set to now + 10 s.
  ManualClock::advance(std::chrono::milliseconds{
      9999});  // 1 ms before expiry  — move the fake clock forward.
  CHECK(cache.get("k") == std::optional<std::string>(
                              "v"));  // Still fresh, so the value comes back.
}

// Reaching the lifetime boundary exactly must count as expired (get uses <=).
TEST_CASE("MemCache entry is gone after TTL elapses",
          "[utils][memcache][ttl]") {
  ManualClock::reset();                                // Clock back to zero.
  ManualStrCache cache(10, std::chrono::seconds{10});  // Ten-second lifetime.

  cache.insert("k", "v");  // Insert at time zero.
  ManualClock::advance(std::chrono::seconds{
      10});  // exactly at expiry boundary  — expiry is now equal to now.
  CHECK(cache.get("k") ==
        std::nullopt);  // The <= comparison makes this a miss.
}

// Re-inserting an existing key restarts its lifetime from the new insert time.
TEST_CASE("MemCache re-insert refreshes TTL", "[utils][memcache][ttl]") {
  ManualClock::reset();                                // Clock back to zero.
  ManualStrCache cache(10, std::chrono::seconds{10});  // Ten-second lifetime.

  cache.insert("k", "first");  // Original expiry is at 10 s.
  ManualClock::advance(
      std::chrono::seconds{5});  // half-way through TTL  — time is now 5 s.
  cache.insert("k",
               "second");  // re-insert resets expiry to now+10s  — new expiry
                           // is 15 s.
  ManualClock::advance(std::chrono::seconds{
      6});  // past original TTL, before new one  — time is now 11 s.
  CHECK(cache.get("k") ==
        std::optional<std::string>(
            "second"));  // Old expiry passed, but the new one has not.

  ManualClock::advance(
      std::chrono::seconds{5});           // now past new TTL  — time is 16 s.
  CHECK(cache.get("k") == std::nullopt);  // Past 15 s, so the entry is a miss.
}

// Different entries can have different TTLs; one may expire while another
// survives.
TEST_CASE("MemCache expired entries are absent while fresh entries remain",
          "[utils][memcache][ttl]") {
  ManualClock::reset();  // Clock back to zero.
  ManualStrCache cache(10,
                       std::chrono::seconds{
                           5});  // Default lifetime of five seconds.

  cache.insert("short", "s");  // Uses the 5 s default, so it expires at 5 s.
  ManualClock::advance(std::chrono::seconds{3});  // Move to 3 s.
  // Insert a second entry with a longer TTL (20s).
  cache
      .insert("long",
              "l",
              std::chrono::seconds{
                  20});  // Explicit TTL overrides the default; expires at 23 s.

  ManualClock::advance(std::chrono::seconds{
      3});  // "short" is now expired (6s > 5s)  — time is 6 s.
  CHECK(cache.get("short") == std::nullopt);  // The short-lived entry is gone.
  CHECK(cache.get("long") == std::optional<std::string>(
                                 "l"));  // The long-lived entry is still there.
}
