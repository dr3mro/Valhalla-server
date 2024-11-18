#pragma once

#include <drogon/drogon.h>
#include <fmt/format.h>

#include <string_view>

#include "typemaps.hpp"  // IWYU pragma: keep
#include "utils/helper/helper.hpp"
namespace api
{
    namespace v2
    {
        template <typename Func, typename Registry, typename... Args>
        static void executeControllerMethod(const Registry& registry, const std::string_view key, Func method, const drogon::HttpRequestPtr& req,
                                            Args&&... args, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
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
                            std::invoke(method, controller.get(), req, callback, std::forward<Args>(args)...);
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
