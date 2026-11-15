// executeControllerMethod runs the controller call on the DbExecutor and answers from there.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

#include "api/v2/basic/common.hpp"
#include "database/databasebusy.hpp"
#include "database/dbexecutor.hpp"
#include "database/dbprobe.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

using namespace std::chrono_literals;
using api::v2::Requester;

namespace
{
    struct ProbeController
    {
        std::thread::id ran_on;
        std::string     seen_body;
        bool            throw_busy = false;
        bool            ran        = false;

        void Do(CALLBACK_&& callback, const Requester&& /*requester*/, std::string_view body)
        {
            ran      = true;
            ran_on   = std::this_thread::get_id();
            seen_body = std::string(body);
            if (throw_busy)
            {
                throw DatabaseBusy();
            }
            std::move(callback)(api::v2::Http::Status::OK, "{\"ok\":true}");
        }
    };

    // Answers from memory when `in_memory`, otherwise reaches for the database the way DatabaseController::executer does.
    struct FastPathController
    {
        std::thread::id ran_on;
        int             runs = 0;

        void Do(CALLBACK_&& callback, const Requester&& /*requester*/, bool in_memory)
        {
            ++runs;
            ran_on = std::this_thread::get_id();
            if (in_memory)
            {
                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "refused without a query");
                return;
            }
            DbProbe::check();
            std::move(callback)(api::v2::Http::Status::OK, "{\"ok\":true}");
        }
    };

    using FastRegistry = std::unordered_map<std::string_view, std::variant<std::shared_ptr<FastPathController>>>;
    using Registry = std::unordered_map<std::string_view, std::variant<std::shared_ptr<ProbeController>>>;

    drogon::HttpRequestPtr makeRequest(const std::string& body)
    {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setBody(body);
        req->attributes()->insert("clientID", static_cast<uint64_t>(7));
        req->attributes()->insert("clientGroup", std::string("users"));
        return req;
    }

    // Lets a blocked worker task go when the test scope ends, even if a REQUIRE threw, so the worker can be joined.
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

TEST_CASE("executeControllerMethod answers 200 through an inline executor", "[api][executor]")
{
    auto     controller = std::make_shared<ProbeController>();
    Registry registry   = {{"probe", controller}};
    DbExecutor executor(0, 4);

    drogon::HttpResponsePtr response;
    auto                    req = makeRequest("hello");
    api::v2::executeControllerMethod(executor, registry, "probe", &ProbeController::Do, req, [&response](const drogon::HttpResponsePtr& r) { response = r; }, req->body());

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k200OK);
    CHECK(controller->seen_body == "hello");
}

TEST_CASE("executeControllerMethod runs the controller on a worker thread and keeps the request data alive", "[api][executor]")
{
    auto     controller = std::make_shared<ProbeController>();
    Registry registry   = {{"probe", controller}};
    DbExecutor executor(1, 4);

    std::promise<drogon::HttpResponsePtr> done;
    auto                                  future = done.get_future();
    {
        auto req = makeRequest("payload that must outlive the handler");
        api::v2::executeControllerMethod(executor, registry, "probe", &ProbeController::Do, req, [&done](const drogon::HttpResponsePtr& r) { done.set_value(r); }, req->body());
    }  // the handler's own reference to the request is gone; the task holds its own

    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    auto response = future.get();
    CHECK(response->statusCode() == drogon::k200OK);
    CHECK(controller->ran_on != std::this_thread::get_id());
    CHECK(controller->seen_body == "payload that must outlive the handler");
}

TEST_CASE("executeControllerMethod answers 503 with Retry-After when the executor queue is full", "[api][executor]")
{
    auto     controller = std::make_shared<ProbeController>();
    Registry registry   = {{"probe", controller}};
    DbExecutor executor(1, 1);

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
    REQUIRE(executor.submit([] {}));  // the queue (size 1) is full now

    drogon::HttpResponsePtr response;
    auto                    req = makeRequest("x");
    api::v2::executeControllerMethod(executor, registry, "probe", &ProbeController::Do, req, [&response](const drogon::HttpResponsePtr& r) { response = r; }, req->body());

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k503ServiceUnavailable);
    CHECK(response->getHeader("Retry-After") == "1");
    CHECK_FALSE(controller->ran);
    gate.now();
}

TEST_CASE("executeControllerMethod turns DatabaseBusy thrown by the controller into 503", "[api][executor]")
{
    auto controller        = std::make_shared<ProbeController>();
    controller->throw_busy = true;
    Registry   registry    = {{"probe", controller}};
    DbExecutor executor(0, 4);

    drogon::HttpResponsePtr response;
    auto                    req = makeRequest("x");
    api::v2::executeControllerMethod(executor, registry, "probe", &ProbeController::Do, req, [&response](const drogon::HttpResponsePtr& r) { response = r; }, req->body());

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k503ServiceUnavailable);
}

TEST_CASE("executeControllerMethod answers 400 for an unknown type", "[api][executor]")
{
    Registry   registry;
    DbExecutor executor(0, 4);

    drogon::HttpResponsePtr response;
    auto                    req = makeRequest("x");
    api::v2::executeControllerMethod(executor, registry, "nope", &ProbeController::Do, req, [&response](const drogon::HttpResponsePtr& r) { response = r; }, req->body());

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k400BadRequest);
}

TEST_CASE("BoundedExecutor shutdown finishes queued work, refuses new work and reports stats", "[concurrency][executor]")
{
    DbExecutor        executor(1, 8);
    std::atomic<int>  ran{0};
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(executor.submit([&] { ++ran; }));
    }
    executor.shutdown();
    CHECK(ran == 5);
    CHECK_FALSE(executor.submit([&] { ++ran; }));
    executor.shutdown();  // second call is harmless
    CHECK(ran == 5);
}

TEST_CASE("fast path answers a request that needs no database on the calling thread", "[api][executor]")
{
    auto         controller = std::make_shared<FastPathController>();
    FastRegistry registry   = {{"fast", controller}};
    DbExecutor   executor(1, 4, true);
    REQUIRE(executor.ioFastPath());

    drogon::HttpResponsePtr response;
    auto                    req = makeRequest("x");
    api::v2::executeControllerMethod(executor, registry, "fast", &FastPathController::Do, req, [&response](const drogon::HttpResponsePtr& r) { response = r; }, true);

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k400BadRequest);
    CHECK(controller->ran_on == std::this_thread::get_id());
    CHECK(controller->runs == 1);
    CHECK(executor.stats().submitted == 0);
}

TEST_CASE("fast path hands a request that needs the database to the executor and runs it from the start", "[api][executor]")
{
    auto         controller = std::make_shared<FastPathController>();
    FastRegistry registry   = {{"fast", controller}};
    DbExecutor   executor(1, 4, true);

    std::promise<drogon::HttpResponsePtr> done;
    auto                                  future = done.get_future();
    auto                                  req    = makeRequest("x");
    api::v2::executeControllerMethod(executor, registry, "fast", &FastPathController::Do, req, [&done](const drogon::HttpResponsePtr& r) { done.set_value(r); }, false);

    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    CHECK(future.get()->statusCode() == drogon::k200OK);
    executor.shutdown();
    CHECK(controller->runs == 2);  // once on the probe, once on the worker (the probe is not active there)
    CHECK(controller->ran_on != std::this_thread::get_id());
}

TEST_CASE("fast path is off without workers", "[api][executor]")
{
    DbExecutor executor(0, 4, true);
    CHECK_FALSE(executor.ioFastPath());
}
