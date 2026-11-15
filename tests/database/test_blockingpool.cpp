#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "database/blockingpool.hpp"

using namespace std::chrono_literals;

namespace
{
    // Gives the item back when it goes out of scope, like DatabaseHanndler does.
    struct Lease
    {
        BlockingPool<int>&  pool;
        std::shared_ptr<int> item;
        Lease(BlockingPool<int>& p, std::chrono::milliseconds timeout) : pool(p), item(p.acquire(timeout)) {}
        Lease(const Lease&)            = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() { pool.release(std::move(item)); }
    };
}  // namespace

TEST_CASE("BlockingPool hands out each item once", "[database][pool]")
{
    BlockingPool<int> pool;
    pool.release(std::make_shared<int>(1));
    pool.release(std::make_shared<int>(2));
    CHECK(pool.idle() == 2);

    auto a = pool.acquire(10ms);
    auto b = pool.acquire(10ms);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK(pool.idle() == 0);
}

TEST_CASE("BlockingPool::acquire returns nullptr after the timeout", "[database][pool]")
{
    BlockingPool<int> pool;
    const auto        started = std::chrono::steady_clock::now();
    CHECK(pool.acquire(50ms) == nullptr);
    CHECK(std::chrono::steady_clock::now() - started >= 50ms);
}

TEST_CASE("BlockingPool::release wakes a waiting acquirer", "[database][pool]")
{
    BlockingPool<int>  pool;
    std::shared_ptr<int> got;
    std::thread        waiter([&] { got = pool.acquire(2s); });

    std::this_thread::sleep_for(50ms);
    pool.release(std::make_shared<int>(7));
    waiter.join();

    REQUIRE(got != nullptr);
    CHECK(*got == 7);
}

TEST_CASE("BlockingPool ignores a null release", "[database][pool]")
{
    BlockingPool<int> pool;
    pool.release(nullptr);
    CHECK(pool.idle() == 0);
}

TEST_CASE("BlockingPool gets the item back when the user throws", "[database][pool]")
{
    BlockingPool<int> pool;
    pool.release(std::make_shared<int>(1));

    auto use = [&]
    {
        Lease lease(pool, 10ms);
        REQUIRE(lease.item != nullptr);
        throw std::runtime_error("query failed");
    };
    CHECK_THROWS_AS(use(), std::runtime_error);
    CHECK(pool.idle() == 1);
}

TEST_CASE("BlockingPool::forEachIdle visits only idle items and runs finish last", "[database][pool]")
{
    BlockingPool<int> pool;
    pool.release(std::make_shared<int>(1));
    pool.release(std::make_shared<int>(2));
    auto busy = pool.acquire(10ms);

    int  visited  = 0;
    bool finished = false;
    pool.forEachIdle([&](std::shared_ptr<int>&) { ++visited; }, [&] { finished = (visited == 1); });
    CHECK(visited == 1);
    CHECK(finished);
}

TEST_CASE("BlockingPool survives many threads sharing few items", "[database][pool][stress]")
{
    constexpr int THREADS = 16;
    constexpr int ITEMS   = 4;

    BlockingPool<std::atomic<int>> pool;
    for (int i = 0; i < ITEMS; ++i)
    {
        pool.release(std::make_shared<std::atomic<int>>(0));
    }

    std::atomic<int> in_use{0};
    std::atomic<int> max_in_use{0};
    std::atomic<int> timeouts{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back(
            [&]
            {
                const auto until = std::chrono::steady_clock::now() + 300ms;
                while (std::chrono::steady_clock::now() < until)
                {
                    auto item = pool.acquire(2s);
                    if (item == nullptr)
                    {
                        ++timeouts;
                        continue;
                    }
                    const int now = ++in_use;
                    int       prev = max_in_use.load();
                    while (now > prev && !max_in_use.compare_exchange_weak(prev, now)) {}
                    ++(*item);
                    --in_use;
                    pool.release(std::move(item));
                }
            });
    }
    for (auto& th : threads)
    {
        th.join();
    }

    CHECK(timeouts == 0);
    CHECK(max_in_use <= ITEMS);
    CHECK(pool.idle() == ITEMS);
}
