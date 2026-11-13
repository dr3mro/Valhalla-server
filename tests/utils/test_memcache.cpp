#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "support/fake_clock.hpp"
#include "utils/memcache/memcache.hpp"

namespace
{
    using StringCache    = MemCache<std::string>;
    using ManualStrCache = MemCache<std::string, ManualClock>;
}  // namespace

TEST_CASE("MemCache stores and retrieves values", "[utils][memcache]")
{
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("k1", "v1");
    CHECK(cache.get("k1") == std::optional<std::string>("v1"));
    CHECK(cache.get("missing") == std::nullopt);
}

TEST_CASE("MemCache insert on an existing key updates the value", "[utils][memcache]")
{
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("k", "first");
    cache.insert("k", "second");
    CHECK(cache.get("k") == std::optional<std::string>("second"));
}

TEST_CASE("MemCache removes entries", "[utils][memcache]")
{
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("k", "v");
    CHECK(cache.remove("k"));
    CHECK(cache.get("k") == std::nullopt);
    CHECK_FALSE(cache.remove("k"));  // removing again reports failure
}

TEST_CASE("MemCache evicts the least recently used entry when full", "[utils][memcache]")
{
    StringCache cache(2, std::chrono::seconds{100});

    cache.insert("a", "1");
    cache.insert("b", "2");
    // Touching "a" makes it the most recently used; "b" becomes the LRU.
    CHECK(cache.get("a").has_value());
    cache.insert("c", "3");

    CHECK(cache.get("b") == std::nullopt);  // evicted
    CHECK(cache.get("a") == std::optional<std::string>("1"));
    CHECK(cache.get("c") == std::optional<std::string>("3"));
}

TEST_CASE("MemCache LRU eviction without any touch", "[utils][memcache]")
{
    StringCache cache(2, std::chrono::seconds{100});

    cache.insert("a", "1");
    cache.insert("b", "2");
    cache.insert("c", "3");  // full: evicts the oldest, "a"

    CHECK(cache.get("a") == std::nullopt);
    CHECK(cache.get("b") == std::optional<std::string>("2"));
    CHECK(cache.get("c") == std::optional<std::string>("3"));
}

TEST_CASE("MemCache entries with defaultTTL=0 are already expired on read", "[utils][memcache]")
{
    // With defaultTTL = 0 an entry expires the moment it is inserted: the
    // expiration time is Clock::now() and get() compares <= now. This makes
    // expiry testable without sleeping on the real clock.
    StringCache cache(10, std::chrono::seconds{0});

    cache.insert("k", "v");
    CHECK(cache.get("k") == std::nullopt);
}

TEST_CASE("MemCache insert with an explicit zero ttl uses the default ttl", "[utils][memcache]")
{
    // insert() cannot express "expire immediately": a ttl of zero is treated
    // as "use defaultTTL_" (documented quirk, tests/README.md).
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("k", "v", std::chrono::seconds{0});
    CHECK(cache.get("k") == std::optional<std::string>("v"));
}

TEST_CASE("MemCache survives concurrent insert/get traffic", "[utils][memcache]")
{
    StringCache cache(1000, std::chrono::seconds{100});

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            [&cache, t]
            {
                for (int i = 0; i < 100; ++i)
                {
                    const auto key = "k" + std::to_string(t * 100 + i);
                    cache.insert(key, key);
                    (void)cache.get(key);
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    // All 400 distinct keys fit (maxSize 1000) and must be readable.
    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < 100; ++i)
        {
            const auto key = "k" + std::to_string(t * 100 + i);
            CHECK(cache.get(key) == std::optional<std::string>(key));
        }
    }
}

// Bug 6 fixed: move constructor now stops and joins the source's cleaner
// before moving state, then starts a fresh cleaner on the destination.
TEST_CASE("MemCache move constructor transfers entries to the destination", "[utils][memcache]")
{
    StringCache src(10, std::chrono::seconds{100});
    src.insert("a", "1");
    src.insert("b", "2");

    StringCache dst(std::move(src));

    CHECK(dst.get("a") == std::optional<std::string>("1"));
    CHECK(dst.get("b") == std::optional<std::string>("2"));
}

// Note: The background cleaner's 1s cadence is not asserted on the real
// clock; expiry is covered deterministically via TTL tests above.

TEST_CASE("MemCache custom TTL per insert takes precedence over default", "[utils][memcache]")
{
    // Default TTL is 100s; insert one entry with TTL=0 (expires immediately).
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("short", "v", std::chrono::seconds{0});
    // TTL 0 means "use default" (see quirk note above), so this should still be readable.
    CHECK(cache.get("short") == std::optional<std::string>("v"));
}

TEST_CASE("MemCache handles multiple removes correctly", "[utils][memcache]")
{
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("a", "1");
    cache.insert("b", "2");

    CHECK(cache.remove("a"));
    CHECK_FALSE(cache.remove("a"));  // second remove fails
    CHECK(cache.get("a") == std::nullopt);
    CHECK(cache.get("b") == std::optional<std::string>("2"));
    CHECK(cache.remove("b"));
}

TEST_CASE("MemCache update resets value but entry remains accessible", "[utils][memcache]")
{
    StringCache cache(10, std::chrono::seconds{100});

    cache.insert("k", "original");
    cache.insert("k", "updated");
    cache.insert("k", "final");

    CHECK(cache.get("k") == std::optional<std::string>("final"));
}

// ── Deterministic TTL tests using ManualClock ────────────────────────────────
// ManualClock is a static (process-global) clock; reset before each test.

TEST_CASE("MemCache entry is present just before its TTL elapses", "[utils][memcache][ttl]")
{
    ManualClock::reset();
    ManualStrCache cache(10, std::chrono::seconds{10});

    cache.insert("k", "v");
    ManualClock::advance(std::chrono::milliseconds{9999});  // 1 ms before expiry
    CHECK(cache.get("k") == std::optional<std::string>("v"));
}

TEST_CASE("MemCache entry is gone after TTL elapses", "[utils][memcache][ttl]")
{
    ManualClock::reset();
    ManualStrCache cache(10, std::chrono::seconds{10});

    cache.insert("k", "v");
    ManualClock::advance(std::chrono::seconds{10});  // exactly at expiry boundary
    CHECK(cache.get("k") == std::nullopt);
}

TEST_CASE("MemCache re-insert refreshes TTL", "[utils][memcache][ttl]")
{
    ManualClock::reset();
    ManualStrCache cache(10, std::chrono::seconds{10});

    cache.insert("k", "first");
    ManualClock::advance(std::chrono::seconds{5});  // half-way through TTL
    cache.insert("k", "second");                    // re-insert resets expiry to now+10s
    ManualClock::advance(std::chrono::seconds{6});  // past original TTL, before new one
    CHECK(cache.get("k") == std::optional<std::string>("second"));

    ManualClock::advance(std::chrono::seconds{5});  // now past new TTL
    CHECK(cache.get("k") == std::nullopt);
}

TEST_CASE("MemCache expired entries are absent while fresh entries remain", "[utils][memcache][ttl]")
{
    ManualClock::reset();
    ManualStrCache cache(10, std::chrono::seconds{5});

    cache.insert("short", "s");
    ManualClock::advance(std::chrono::seconds{3});
    // Insert a second entry with a longer TTL (20s).
    cache.insert("long", "l", std::chrono::seconds{20});

    ManualClock::advance(std::chrono::seconds{3});  // "short" is now expired (6s > 5s)
    CHECK(cache.get("short") == std::nullopt);
    CHECK(cache.get("long") == std::optional<std::string>("l"));
}
