#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "utils/memcache/memcache.hpp"

namespace
{
    using StringCache = MemCache<std::string>;
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
        threads.emplace_back([&cache, t]
        {
            for (int i = 0; i < 100; ++i)
            {
                const auto key = "k" + std::to_string(t * 100 + i);
                cache.insert(key, key);
                (void)cache.get(key);
            }
        });
    }
    for (auto &thread : threads)
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

// NOTE: the move constructor is intentionally not tested: it moves the
// cleaner thread out of the source object, whose destructor then calls
// join() on a non-joinable thread and throws std::system_error - see
// tests/README.md, known bug 6. The background cleaner's 1s cadence is also
// not asserted on the real clock; expiry is covered deterministically via
// defaultTTL = 0 above.
