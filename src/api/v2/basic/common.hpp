#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "api/v2/helper/helper.hpp"
#include "database/databasebusy.hpp"
#include "database/dbexecutor.hpp"
#include "database/dbprobe.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

#define RATELIMIT "api::v2::Filters::RateLimit"
#define AUTH "api::v2::Filters::Auth"
#define ELAPSED "api::v2::MiddleWares::ElapsedTime"

#define INSECURE RATELIMIT, ELAPSED
#define SECURE INSECURE, AUTH

namespace api::v2
{
    // stoll(req->getParameter(name)) throws std::invalid_argument/out_of_range when the parameter is
    // missing or non-numeric. That throw happens in the route handler's argument list, BEFORE
    // executeControllerMethod's own try/catch is ever entered, so it used to escape all the way up
    // through Drogon's dispatch as a raw, empty-body 500. This parses the id parameter safely and
    // replies with a normal 400 instead; callers must check the returned optional and return early
    // (without calling executeControllerMethod) when it is empty.
    inline std::optional<int64_t> parseIdParam(
        const drogon::HttpRequestPtr& req, const std::string& name, std::function<void(const drogon::HttpResponsePtr&)>& callback)
    {
        const std::string& raw = req->getParameter(name);
        try
        {
            return std::stoll(raw);
        }
        catch (const std::exception&)
        {
            Helper::errorResponse(drogon::k400BadRequest, fmt::format("Missing or invalid '{}' parameter.", name), std::move(callback));
            return std::nullopt;
        }
    }

    // Runs the controller method on the database executor, so the blocking permission check and query never hold a Drogon IO
    // thread. The request, the arguments and the response callback are captured by value, because the work finishes after
    // this function has returned, on another thread. A full queue is answered with 503 right here.
    template <typename Func, typename Registry, typename... Args>
    static void executeControllerMethod(DbExecutor& executor, const Registry& registry, const std::string_view key, Func method, const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback, Args&&... args)
    {
        auto ctl = registry.find(key);
        if (ctl == registry.end())
        {
            Helper::errorResponse(drogon::k400BadRequest, fmt::format("Type mapping not found for: {}", key), std::move(callback));
            return;
        }

        try
        {
            std::visit(
                [&](const auto& controller)
                {
                    CALLBACK_ mcb = [callback](int code, const std::string& content)
                    {
                        using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
                        switch (code)
                        {
                            case api::v2::Http::Status::OK:
                                Helper::successResponse(content, Reply(callback));
                                break;
                            case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                Helper::failureResponse(content, Reply(callback));
                                break;
                            default:
                                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, Reply(callback));
                                break;
                        }
                    };

                    // Read what the handler needs from the request now; the rest runs on a worker.
                    Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                    auto packed = std::make_tuple(std::decay_t<Args>(args)...);

                    // Fast path: run the call here first. A request that is answered from memory (bad body, refused by the
                    // permission cache) never queues; the first query throws WouldBlock before anything was written, and
                    // the request goes to the executor and runs again from the start.
                    if (executor.ioFastPath())
                    {
                        try
                        {
                            DbProbe     probe;
                            CALLBACK_   probe_cb = mcb;  // its own copy: the real one must still be there if the probe gives up
                            std::apply([&](auto&... unpacked) { std::invoke(method, controller.get(), std::move(probe_cb), std::move(requester), unpacked...); }, packed);
                            return;
                        }
                        catch (const WouldBlock&)
                        {
                            // needs the database: queue it below
                        }
                        catch (const std::exception& e)
                        {
                            Helper::failureResponse(e.what(), std::move(callback));
                            return;
                        }
                    }

                    runOnDbExecutor(executor, std::move(mcb),
                        [method, target = controller.get(), req, callback, requester = std::move(requester), packed = std::move(packed)](CALLBACK_&& cb) mutable
                        {
                            try
                            {
                                std::apply([&](auto&... unpacked) { std::invoke(method, target, std::move(cb), std::move(requester), unpacked...); }, packed);
                            }
                            catch (const DatabaseBusy& e)
                            {
                                Helper::errorResponse(drogon::k503ServiceUnavailable, e.what(), std::move(callback));
                            }
                            catch (const std::exception& e)
                            {
                                Helper::failureResponse(e.what(), std::move(callback));
                            }
                        });
                },
                ctl->second);
        }
        catch (const std::exception& e)
        {
            Helper::failureResponse(e.what(), std::move(callback));
        }
    }
}  // namespace api::v2
