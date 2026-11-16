#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <atomic>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "server/cors.hpp"
#include "utils/memcache/memcache.hpp"
#include "utils/threadsafequeue/threadsafequeue.hpp"

namespace
{
    bool benchmarksRequested() { return std::getenv("VALHALLA_RUN_BENCHMARKS") != nullptr; }

    std::atomic<bool>        g_countAllocations{false};
    std::atomic<std::size_t> g_allocationCount{0};

    void* countedAllocate(std::size_t size)
    {
        if (g_countAllocations.load(std::memory_order_relaxed))
        {
            g_allocationCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (void* ptr = std::malloc(size))
        {
            return ptr;
        }
        throw std::bad_alloc();
    }

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

void* operator new(std::size_t size) { return countedAllocate(size); }
void* operator new(std::size_t size, std::align_val_t) { return countedAllocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAllocate(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAllocate(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size) { return countedAllocate(size); }
void* operator new[](std::size_t size, std::align_val_t) { return countedAllocate(size); }

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }

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

TEST_CASE("Benchmark: Cors::apply", "[.benchmark]")
{
    if (!benchmarksRequested())
    {
        SKIP();
    }

    auto requestGet = drogon::HttpRequest::newHttpRequest();
    requestGet->setMethod(drogon::Get);

    auto requestPreflight = drogon::HttpRequest::newHttpRequest();
    requestPreflight->setMethod(drogon::Options);
    requestPreflight->addHeader("Access-Control-Request-Headers", "content-type, x-custom");

    BENCHMARK("Cors::apply GET")
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        Cors::apply(requestGet, response);
        return response;
    };

    BENCHMARK("Cors::apply preflight")
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        Cors::apply(requestPreflight, response);
        return response;
    };

    BENCHMARK("old behaviour, five addHeader calls")
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
        response->addHeader("Access-Control-Expose-Headers", "Retry-After, Content-Length");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, X-Requested-With");
        response->addHeader("Access-Control-Max-Age", "7200");
        return response;
    };

    BENCHMARK("response creation only") { return drogon::HttpResponse::newHttpResponse(); };

    auto countAllocations = [&](auto&& fn)
    {
        g_allocationCount.store(0, std::memory_order_relaxed);
        g_countAllocations.store(true, std::memory_order_relaxed);
        for (int i = 0; i < 10000; ++i)
        {
            fn();
        }
        g_countAllocations.store(false, std::memory_order_relaxed);
        return g_allocationCount.load(std::memory_order_relaxed) / 10000;
    };

    WARN("Cors::apply GET allocations/call: " << countAllocations(
             [&]()
             {
                 auto response = drogon::HttpResponse::newHttpResponse();
                 Cors::apply(requestGet, response);
             }));
    WARN("Cors::apply preflight allocations/call: " << countAllocations(
             [&]()
             {
                 auto response = drogon::HttpResponse::newHttpResponse();
                 Cors::apply(requestPreflight, response);
             }));
    WARN("old behaviour, five addHeader calls allocations/call: " << countAllocations(
             [&]()
             {
                 auto response = drogon::HttpResponse::newHttpResponse();
                 response->addHeader("Access-Control-Allow-Origin", "*");
                 response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, HEAD, OPTIONS");
                 response->addHeader("Access-Control-Expose-Headers", "Retry-After, Content-Length");
                 response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept, X-Requested-With");
                 response->addHeader("Access-Control-Max-Age", "7200");
             }));
    WARN("response creation only allocations/call: " << countAllocations([&]() { (void)drogon::HttpResponse::newHttpResponse(); }));
}
