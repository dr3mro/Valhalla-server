#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "database/inuseguard.hpp"

TEST_CASE("InUseGuard acquires an uncontended resource immediately", "[database][inuseguard]")
{
    std::atomic<bool>     in_use{false};
    std::mutex            mtx;
    std::condition_variable cv;

    {
        InUseGuard guard(in_use, mtx, cv);
        CHECK(in_use.load());
    }

    // The destructor releases the resource and wakes waiters.
    CHECK_FALSE(in_use.load());
}

TEST_CASE("InUseGuard waits out the timeout when the resource is busy", "[database][inuseguard]")
{
    std::atomic<bool>       in_use{true};  // somebody else holds the resource
    std::mutex              mtx;
    std::condition_variable cv;

    // Use a 50 ms timeout so the test completes quickly while still verifying
    // that the guard waits at least that long before taking ownership.
    constexpr auto kTimeout = std::chrono::milliseconds{50};

    const auto start = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(InUseGuard(in_use, mtx, cv, kTimeout), std::runtime_error);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed >= kTimeout);
    CHECK(in_use.load());
}

TEST_CASE("InUseGuard destructor notify wakes a concurrent waiter", "[database][inuseguard]")
{
    std::atomic<bool>       in_use{false};
    std::mutex              mtx;
    std::condition_variable cv;

    bool acquired = false;

    std::optional<std::thread> waiter;

    {
        InUseGuard holder(in_use, mtx, cv);
        CHECK(in_use.load());

        // Start the waiter after holder has set in_use=true.
        waiter.emplace([&]
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::seconds{2}, [&] { return !in_use.load(); });
            acquired = !in_use.load();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        // holder's destructor runs here: stores false and notifies all.
    }

    waiter->join();
    CHECK(acquired);
}
