#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

#include "gatekeeper/passwordcrypt/passwordworker.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"

using namespace std::chrono_literals;

namespace
{
    // Lets a blocked worker task go when the test scope ends, even if a REQUIRE threw, so the worker can be joined.
    struct ReleaseOnExit
    {
        std::promise<void>& promise;
        bool                released = false;
        void                now()
        {
            if (!released)
            {
                released = true;
                promise.set_value();
            }
        }
        ~ReleaseOnExit() { now(); }
    };
}  // namespace

TEST_CASE("PasswordWorker with zero workers runs the task inline", "[gatekeeper][passwordworker]")
{
    PasswordWorker worker(0, 4);
    CHECK(worker.inlineMode());

    std::thread::id ran_on;
    REQUIRE(worker.submit([&] { ran_on = std::this_thread::get_id(); }));
    CHECK(ran_on == std::this_thread::get_id());
}

TEST_CASE("PasswordWorker runs tasks on its own threads", "[gatekeeper][passwordworker]")
{
    PasswordWorker worker(2, 8);
    CHECK_FALSE(worker.inlineMode());

    std::promise<std::thread::id> ran_on;
    auto                          done = ran_on.get_future();
    REQUIRE(worker.submit([&] { ran_on.set_value(std::this_thread::get_id()); }));

    REQUIRE(done.wait_for(2s) == std::future_status::ready);
    CHECK(done.get() != std::this_thread::get_id());
}

TEST_CASE("PasswordWorker refuses work when the queue is full", "[gatekeeper][passwordworker]")
{
    std::promise<void>        started_promise;
    std::future<void>         started = started_promise.get_future();
    std::promise<void>        release_promise;
    std::shared_future<void>  release = release_promise.get_future().share();
    std::atomic<int> ran{0};

    {
        PasswordWorker worker(1, 1);
        ReleaseOnExit  gate{release_promise};

        // Occupies the only worker until released.
        REQUIRE(worker.submit(
            [&]
            {
                started_promise.set_value();
                release.wait();
                ++ran;
            }));
        started.wait();

        REQUIRE(worker.submit([&] { ++ran; }));  // waits in the queue (1 of 1)
        CHECK(worker.pending() == 1);
        CHECK_FALSE(worker.submit([&] { ++ran; }));  // queue full

        gate.now();
    }  // destructor finishes the queued task

    CHECK(ran == 2);
}

TEST_CASE("PasswordWorker never runs more tasks at once than it has workers", "[gatekeeper][passwordworker]")
{
    constexpr int WORKERS = 2;
    constexpr int TASKS   = 12;

    std::atomic<int> running{0};
    std::atomic<int> peak{0};
    std::atomic<int> done{0};
    {
        PasswordWorker worker(WORKERS, TASKS);
        for (int i = 0; i < TASKS; ++i)
        {
            REQUIRE(worker.submit(
                [&]
                {
                    const int now  = ++running;
                    int       prev = peak.load();
                    while (now > prev && !peak.compare_exchange_weak(prev, now)) {}
                    std::this_thread::sleep_for(5ms);
                    --running;
                    ++done;
                }));
        }
    }
    CHECK(done == TASKS);
    CHECK(peak <= WORKERS);
}

TEST_CASE("A throwing task does not stop the worker", "[gatekeeper][passwordworker]")
{
    std::atomic<int> ran{0};
    {
        PasswordWorker worker(1, 4);
        REQUIRE(worker.submit([] { throw std::runtime_error("task failed"); }));
        REQUIRE(worker.submit([&] { ++ran; }));
    }
    CHECK(ran == 1);
}

TEST_CASE("runOnPasswordWorker hands the callback to the task", "[gatekeeper][passwordworker]")
{
    PasswordWorker worker(0, 4);
    int            code = -1;
    std::string    body;

    runOnPasswordWorker(worker, [&](int c, const std::string& b)
        {
            code = c;
            body = b;
        },
        [](CALLBACK_&& callback) { callback(api::v2::Http::Status::OK, "done"); });

    CHECK(code == api::v2::Http::Status::OK);
    CHECK(body == "done");
}

TEST_CASE("runOnPasswordWorker answers 503 and skips the task when the queue is full", "[gatekeeper][passwordworker]")
{
    std::promise<void>       started_promise;
    std::future<void>        started = started_promise.get_future();
    std::promise<void>       release_promise;
    std::shared_future<void> release = release_promise.get_future().share();
    int        code     = -1;
    bool       body_ran = false;
    {
        PasswordWorker worker(1, 1);
        ReleaseOnExit  gate{release_promise};
        REQUIRE(worker.submit(
            [&]
            {
                started_promise.set_value();
                release.wait();
            }));
        started.wait();
        REQUIRE(worker.submit([] {}));  // fills the queue

        runOnPasswordWorker(worker, [&](int c, const std::string&) { code = c; }, [&](CALLBACK_&&) { body_ran = true; });

        CHECK(code == api::v2::Http::Status::SERVICE_UNAVAILABLE);
        CHECK_FALSE(body_ran);
        gate.now();
    }
}
