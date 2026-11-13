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
#include <utility>

#include "api/v2/helper/helper.hpp"
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

    template <typename Func, typename Registry, typename... Args>
    static void executeControllerMethod(const Registry& registry, const std::string_view key, Func method, const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback, Args&&... args)
    {
        auto ctl = registry.find(key);
        if (ctl != registry.end())
        {
            try
            {
                std::visit(
                    [&](const auto& controller)
                    {
                        CALLBACK_ mcb = [&callback](int code, const std::string& content)
                        {
                            switch (code)
                            {
                                case api::v2::Http::Status::OK:
                                    Helper::successResponse(content, std::move(callback));
                                    break;
                                case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                    Helper::failureResponse(content, std::move(callback));
                                    break;
                                default:
                                    Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, std::move(callback));
                                    break;
                            }
                        };

                        Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                        std::invoke(method, controller.get(), std::move(mcb), std::move(requester), std::forward<Args>(args)...);
                        return;
                    },
                    ctl->second);
            }
            catch (const std::exception& e)
            {
                Helper::failureResponse(e.what(), std::move(callback));
            }
        }
        else
        {
            Helper::errorResponse(drogon::k400BadRequest, fmt::format("Type mapping not found for: {}", key), std::move(callback));
        }
    }
}  // namespace api::v2
