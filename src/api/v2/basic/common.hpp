#pragma once

#include <drogon/drogon.h>
#include <fmt/format.h>

#include <string_view>

#include "api/v2/helper/helper.hpp"

#define RATELIMIT "api::v2::Filters::RateLimit"
#define AUTH "api::v2::Filters::Auth"
#define ELAPSED "api::v2::MiddleWares::ElapsedTime"

#define INSECURE RATELIMIT, ELAPSED
#define SECURE INSECURE, AUTH

#define CALLBACKSIGNATURE std::function<void(int, const std::string&)>
#
namespace api
{
    namespace v2
    {

        template <typename Func, typename Registry, typename... Args>
        static void executeControllerMethod(const Registry& registry, const std::string_view key, Func method,
                                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, Args&&... args)
        {
            auto it = registry.find(key);
            if (it != registry.end())
            {
                try
                {
                    std::visit(
                        [&](const auto& controller)
                        {
                            CALLBACKSIGNATURE mcb = [&callback](int code, const std::string& content)
                            {
                                switch (code)
                                {
                                    case 200:
                                        Helper::successResponse(content, std::move(callback));
                                        break;
                                    case 500:
                                        Helper::failureResponse(content, std::move(callback));
                                        break;
                                    default:
                                        Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, std::move(callback));
                                        break;
                                }
                            };
                            std::invoke(method, controller.get(), std::move(mcb), std::forward<Args>(args)...);
                            return;
                        },
                        it->second);
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
    }  // namespace v2

}  // namespace api
