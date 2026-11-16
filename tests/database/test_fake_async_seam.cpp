// The FakeDatabaseController coroutine seam: DB_ASYNC=true paths must be testable hermetically,
// without a real PostgreSQL connection or the Drogon application framework.
#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <database/databasebusy.hpp>
#include <jsoncons/json.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "support/fake_db.hpp"

namespace
{
/// Hermetic driver: runs one `drogon::Task<void>` body on a private event loop
/// thread and joins when it finishes.
template <typename F>
void runOnLoop(F&& body)
{
    // trantor::EventLoop must be constructed on the thread that runs it.
    trantor::EventLoop* loop = nullptr;
    std::promise<void>  running;
    std::thread thread([&loop, &running]
        {
            trantor::EventLoop loopThread;
            loop = &loopThread;
            running.set_value();
            loopThread.loop();
        });
    running.get_future().wait();

    std::atomic<bool> done{false};
    // The launcher must be a drogon::AsyncTask (fire-and-forget); the awaited
    // bodies return drogon::Task, which is awaitable.
    loop->queueInLoop([loop, &done, &body]() -> drogon::AsyncTask
        {
            co_await body();
            done = true;
            loop->quit();
        });
    while (!done.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    thread.join();
}
}  // namespace

TEST_CASE("FakeDatabaseController coroutines answer from the sync result fields", "[database][asyncdb][fake]")
{
    auto fake = std::make_shared<FakeDatabaseController>();
    fake->read_result.emplace(jsoncons::json::parse(R"({"id":7,"active":true})"));

    runOnLoop([&]() -> drogon::Task<void>
        {
            auto result = co_await fake->executeReadQueryCoro("SELECT 1");
            REQUIRE(result.has_value());
            CHECK(result->at("id").as<uint64_t>() == 7);
        });
    CHECK(fake->coro_calls == 1);
    CHECK(fake->query_calls == 0);  // nothing was written
}

TEST_CASE("FakeDatabaseController coroutines propagate DatabaseBusy when busy", "[database][asyncdb][fake]")
{
    auto fake  = std::make_shared<FakeDatabaseController>();
    fake->busy = true;

    bool threw = false;
    runOnLoop([&]() -> drogon::Task<void>
        {
            try
            {
                (void)(co_await fake->getPermissionsCoro("SELECT 1"));
            }
            catch (const DatabaseBusy&)
            {
                threw = true;
            }
        });
    CHECK(threw);
}
