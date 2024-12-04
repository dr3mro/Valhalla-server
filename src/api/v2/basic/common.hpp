#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <fmt/format.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "api/v2/helper/helper.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "gatekeeper/permissionmanager/context.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#define RATELIMIT "api::v2::Filters::RateLimit"
#define AUTH "api::v2::Filters::Auth"
#define ELAPSED "api::v2::MiddleWares::ElapsedTime"

#define INSECURE RATELIMIT, ELAPSED
#define SECURE INSECURE, AUTH

namespace api
{
    namespace v2
    {
        inline std::optional<Context> createContext(
            const drogon::HttpRequestPtr& req, const Context::Type operationType, const std::string& destination_type)
        {
            if (operationType == Context::Type::NONE)
            {
                return std::nullopt;
            }
            uint64_t    clientID = req->attributes()->get<uint64_t>("clientID");
            std::string group    = req->attributes()->get<std::string>("clientGroup");

            Context::Source      source(group, clientID);
            Context::Destination destination(destination_type, req->getBody());

            return Context(operationType, source, destination);
        }

        template <typename Func, typename Registry, typename... Args>
        static void executeControllerMethod(const Registry& registry, const std::string_view key, Func method,
            const std::optional<Context>&& ctx, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            Args&&... args)
        {
            std::shared_ptr<GateKeeper> gateKeeper_ = Store::getObject<GateKeeper>();
            if (ctx.has_value() && !gateKeeper_->hasPermission(ctx.value()))
            {
                Helper::errorResponse(
                    drogon::k403Forbidden, "You are not allowed to perform this action", std::move(callback));
                return;
            }

            auto it = registry.find(key);
            if (it != registry.end())
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
                                        Helper::errorResponse(
                                            static_cast<drogon::HttpStatusCode>(code), content, std::move(callback));
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
                Helper::errorResponse(
                    drogon::k400BadRequest, fmt::format("Type mapping not found for: {}", key), std::move(callback));
            }
        }

    }  // namespace v2

}  // namespace api
