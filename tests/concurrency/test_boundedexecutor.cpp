#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "concurrency/boundedexecutor.hpp"
#include "database/dbexecutor.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"

using namespace std::chrono_literals;

namespace
{
    // Lets a blocked task go when the test scope ends, even if a REQUIRE threw, so the worker can be joined.
    struct ReleaseOnExit
    {
        std::promise<void>& promise;
        bool                released = false;
        BoundedExecutor*    executor = nullptr;  // joined before the objects the blocked task uses go away
        void                now()
        {
            if (!released)
            {
                released = true;
                promise.set_value();
            }
        }
        ~ReleaseOnExit()
        {
            now();
            if (executor != nullptr)
            {
                executor->shutdown();
            }
        }
    };
}  // namespace

TEST_CASE("BoundedExecutor with zero workers runs the task inline", "[concurrency][executor]")
{
    BoundedExecutor executor(0, 4, "test");
    CHECK(executor.inlineMode());
    CHECK(executor.workerCount() == 0);

    std::thread::id ran_on;
    REQUIRE(executor.submit([&] { ran_on = std::this_thread::get_id(); }));
    CHECK(ran_on == std::this_thread::get_id());
}

TEST_CASE("BoundedExecutor runs tasks on its own threads and knows which thread is a worker", "[concurrency][executor]")
{
    BoundedExecutor executor(2, 8, "test");
    CHECK(executor.workerCount() == 2);
    CHECK_FALSE(executor.onWorkerThread());

    std::promise<std::pair<std::thread::id, bool>> done;
    auto                                           future = done.get_future();
    REQUIRE(executor.submit([&] { done.set_value({std::this_thread::get_id(), executor.onWorkerThread()}); }));

    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    const auto [thread_id, on_worker] = future.get();
    CHECK(thread_id != std::this_thread::get_id());
    CHECK(on_worker);

    BoundedExecutor other(1, 1, "other");
    std::promise<bool> other_says;
    auto               other_future = other_says.get_future();
    REQUIRE(other.submit([&] { other_says.set_value(executor.onWorkerThread()); }));
    REQUIRE(other_future.wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(other_future.get());  // a worker of another executor is not a worker of this one
}

TEST_CASE("BoundedExecutor refuses work when the queue is full", "[concurrency][executor]")
{
    std::promise<void> started_promise;
    auto               started = started_promise.get_future();
    std::promise<void> release_promise;
    auto               release = release_promise.get_future().share();
    std::atomic<int>   ran{0};

    {
        BoundedExecutor executor(1, 1, "test");
        ReleaseOnExit   gate{release_promise, false, &executor};

        REQUIRE(executor.submit(
            [&]
            {
                started_promise.set_value();
                release.wait();
                ++ran;
            }));
        REQUIRE(started.wait_for(5s) == std::future_status::ready);  // the only worker is busy now

        CHECK(executor.submit([&] { ++ran; }));        // fills the queue (size 1)
        CHECK_FALSE(executor.submit([&] { ++ran; }));  // refused
        CHECK(executor.pending() == 1);

        gate.now();
    }  // the destructor finishes the queued task and joins
    CHECK(ran == 2);
}

TEST_CASE("BoundedExecutor survives a task that throws", "[concurrency][executor]")
{
    BoundedExecutor executor(1, 4, "test");
    REQUIRE(executor.submit([] { throw std::runtime_error("boom"); }));

    std::promise<void> done;
    auto               future = done.get_future();
    REQUIRE(executor.submit([&] { done.set_value(); }));
    CHECK(future.wait_for(5s) == std::future_status::ready);
}

TEST_CASE("runOnDbExecutor answers 503 when the queue is full and never runs the body", "[concurrency][executor]")
{
    std::promise<void> started_promise;
    auto               started = started_promise.get_future();
    std::promise<void> release_promise;
    auto               release = release_promise.get_future().share();

    DbExecutor    executor(1, 1);
    ReleaseOnExit gate{release_promise, false, &executor};

    REQUIRE(executor.submit(
        [&]
        {
            started_promise.set_value();
            release.wait();
        }));
    REQUIRE(started.wait_for(5s) == std::future_status::ready);
    REQUIRE(executor.submit([] {}));  // fills the queue

    int         code = 0;
    std::string text;
    bool        body_ran = false;
    runOnDbExecutor(executor,
        [&](int c, const std::string& t)
        {
            code = c;
            text = t;
        },
        [&](CALLBACK_&&) { body_ran = true; });

    CHECK(code == static_cast<int>(api::v2::Http::Status::SERVICE_UNAVAILABLE));
    CHECK_FALSE(text.empty());
    CHECK_FALSE(body_ran);
    gate.now();
}

TEST_CASE("runOnDbExecutor hands the callback to the body when it is accepted", "[concurrency][executor]")
{
    DbExecutor executor(0, 4);  // inline

    int  code = 0;
    runOnDbExecutor(executor,
        [&](int c, const std::string&) { code = c; },
        [](CALLBACK_&& cb) { std::move(cb)(api::v2::Http::Status::OK, "ok"); });
    CHECK(code == static_cast<int>(api::v2::Http::Status::OK));
}

TEST_CASE("BoundedExecutor: a task does not wait behind a slow one while another worker is free", "[concurrency][executor]")
{
    BoundedExecutor executor(2, 64, "test");

    std::atomic<int>   done{0};
    std::promise<void> started_promise;
    auto               started = started_promise.get_future();
    std::promise<void> release_promise;
    auto               release = release_promise.get_future().share();
    ReleaseOnExit      gate{release_promise, false, &executor};

    REQUIRE(executor.submit(
        [&]
        {
            started_promise.set_value();
            release.wait();
        }));
    REQUIRE(started.wait_for(5s) == std::future_status::ready);

    // The first worker is stuck; every later task must still finish, whichever worker round-robin picks first.
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(executor.submit([&] { ++done; }));
    }
    for (int waited = 0; waited < 500 && done < 20; ++waited)
    {
        std::this_thread::sleep_for(10ms);
    }
    CHECK(done == 20);
    gate.now();
}

TEST_CASE("BoundedExecutor: the queue limit is shared by all workers and every task runs once", "[concurrency][executor]")
{
    BoundedExecutor executor(4, 8, "test");

    std::atomic<int>   started{0};
    std::atomic<int>   ran{0};
    std::promise<void> release_promise;
    auto               release = release_promise.get_future().share();
    ReleaseOnExit      gate{release_promise, false, &executor};

    for (int i = 0; i < 4; ++i)  // keep every worker busy
    {
        REQUIRE(executor.submit(
            [&]
            {
                ++started;
                release.wait();
                ++ran;
            }));
    }
    for (int waited = 0; waited < 500 && started < 4; ++waited)
    {
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(started == 4);

    int accepted = 0;
    for (int i = 0; i < 20; ++i)
    {
        if (executor.submit([&] { ++ran; }))
        {
            ++accepted;
        }
    }
    CHECK(accepted == 8);
    CHECK(executor.pending() == 8);
    gate.now();
    executor.shutdown();
    CHECK(ran == 4 + accepted);
    CHECK(executor.pending() == 0);
}
