#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "gatekeeper/sqlinjectiondetectoor/sqlinjectiondetector.hpp"
#include "utils/memcache/memcache.hpp"
#include "utils/threadsafequeue/threadsafequeue.hpp"

// Benchmarks for the hot paths of the server. They are behind a hidden
// `[.benchmark]` tag (not run by default) AND a `VALHALLA_RUN_BENCHMARKS`
// environment variable, so `ctest` stays fast and hermetic. Run them with:
//   VALHALLA_RUN_BENCHMARKS=1 ./build/Debug/tests/unit_tests "[.benchmark]"

namespace
{
    bool benchmarksRequested() { return std::getenv("VALHALLA_RUN_BENCHMARKS") != nullptr; }

    /// Milliseconds for a repeated callable.
    template <typename Callable>
    long long measure(Callable &&callable, const int iterations)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            callable(i);
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    }
}  // namespace

TEST_CASE("Benchmark: SqlInjectionDetector::isQuerySqlInjection", "[.benchmark]")
{
    if (!benchmarksRequested())
    {
        SKIP();
    }

    SqlInjectionDetector::initialize();
    const std::vector<std::string> queries = {"john_doe", "P0001", "abc' OR 1=1", "'; DROP TABLE users; --", "1; DELETE FROM users"};

    const auto milliseconds = measure(
        [&](int)
        {
            for (const auto &query : queries)
            {
                (void)SqlInjectionDetector::isQuerySqlInjection(query);
            }
        },
        10000);

    INFO("isQuerySqlInjection 5 queries x 10000 iterations: " << milliseconds << " ms");
    CHECK(milliseconds >= 0);
}

TEST_CASE("Benchmark: ThreadSafeQueue push/pop round trips", "[.benchmark]")
{
    if (!benchmarksRequested())
    {
        SKIP();
    }

    ThreadSafeQueue<int> queue;
    const auto           milliseconds = measure([&](int i) { queue.push(i); }, 100000);

    long long popMs = 0;
    {
        const auto start = std::chrono::steady_clock::now();
        while (queue.pop().has_value())
        {
        }
        popMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    }

    INFO("push 100000 ints: " << milliseconds << " ms; drain: " << popMs << " ms");
    CHECK(queue.empty());
}

TEST_CASE("Benchmark: MemCache insert/get round trips", "[.benchmark]")
{
    if (!benchmarksRequested())
    {
        SKIP();
    }

    MemCache<std::string> cache(100000, std::chrono::seconds{60});

    const auto insertMs = measure([&](int i) { cache.insert("k" + std::to_string(i), "v" + std::to_string(i)); }, 50000);

    const auto getMs = measure([&](int i) { (void)cache.get("k" + std::to_string(i % 50000)); }, 50000);

    INFO("insert 50000: " << insertMs << " ms; get 50000: " << getMs << " ms");
    CHECK(insertMs >= 0);
    CHECK(getMs >= 0);
}
