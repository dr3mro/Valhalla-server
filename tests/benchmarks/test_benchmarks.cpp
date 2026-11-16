#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include "server/cors.hpp"
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
    long long measure(Callable&& callable, const int iterations)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            callable(i);
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    }
}  // namespace

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

TEST_CASE("Benchmark: Cors::apply on a fresh response", "[.benchmark]")
{
    if (!benchmarksRequested())
    {
        SKIP();
    }

    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);

    const auto applyMs = measure(
        [&](int /*i*/)
        {
            auto response = drogon::HttpResponse::newHttpResponse();
            Cors::apply(request, response);
        },
        10000);

    const auto oldMs = measure(
        [&](int /*i*/)
        {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->addHeader("Access-Control-Allow-Origin", "*");
            response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
            response->addHeader("Access-Control-Expose-Headers", "Retry-After, Content-Length");
            response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, X-Requested-With");
        },
        10000);

    INFO("Cors::apply 10000 calls: " << applyMs << " ms; five addHeader calls 10000 times: " << oldMs << " ms");
    CHECK(applyMs >= 0);
    CHECK(oldMs >= 0);
}
