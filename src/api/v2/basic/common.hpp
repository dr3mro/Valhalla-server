#pragma once

#include <drogon/drogon.h>
#include <fmt/format.h>

#include <string_view>

#include "utils/helper/helper.hpp"

#define RATELIMIT "api::v2::Filters::RateLimit"
#define AUTH "api::v2::Filters::Auth"
#define ELAPSED "api::v2::MiddleWares::ElapsedTime"

namespace api
{
    namespace v2
    {

        template <typename Func, typename Registry, typename... Args>
        static void executeControllerMethod(const Registry& registry, const std::string_view key, Func method,
                                            std::function<void(const drogon::HttpResponsePtr&)>&& callback, Args&&... args)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();

            auto it = registry.find(key);
            if (it != registry.end())
            {
                try
                {
                    std::visit(
                        [&](const auto& controller)
                        {
                            // Use std::invoke on the dereferenced controller object
                            std::invoke(method, controller.get(), std::move(callback), std::forward<Args>(args)...);
                        },
                        it->second);
                }
                catch (const std::exception& e)
                {
                    Helper::failureResponse(e.what(), callback);
                }
            }
            else
            {
                Helper::errorResponse(drogon::k400BadRequest, fmt::format("Type mapping not found for: {}", key), callback);
            }
        }
    }  // namespace v2

}  // namespace api
