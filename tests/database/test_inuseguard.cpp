#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
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

TEST_CASE("InUseGuard waits out the 1 second timeout when the resource is busy", "[database][inuseguard]")
{
    std::atomic<bool>       in_use{true};  // somebody else holds the resource
    std::mutex              mtx;
    std::condition_variable cv;

    const auto start = std::chrono::steady_clock::now();
    {
        InUseGuard guard(in_use, mtx, cv);  // blocks ~1s inside wait_for

        const auto elapsed = std::chrono::steady_clock::now() - start;

        // The guard gives up waiting after roughly its 1 second timeout, then
        // marks the resource in use anyway (it took ownership unconditionally).
        CHECK(elapsed >= std::chrono::milliseconds{900});
        CHECK(in_use.load());
    }

    // Releasing works as usual.
    CHECK_FALSE(in_use.load());
}

TEST_CASE("InUseGuard destructor notify wakes a concurrent waiter", "[database][inuseguard]")
{
    std::atomic<bool>       in_use{false};
    std::mutex              mtx;
    std::condition_variable cv;

    bool acquired = false;

    {
        InUseGuard holder(in_use, mtx, cv);
        CHECK(in_use.load());

        // A second guard in another thread would wait ~1s for the resource;
        // releasing the holder notifies it early. We verify the notify path
        // with a plain waiter instead of a second 1s guard.
        std::thread waiter([&]
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::seconds{2}, [&] { return !in_use.load(); });
            acquired = !in_use.load();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        // holder's destructor runs here: stores false and notifies all.
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    CHECK(acquired);
}
