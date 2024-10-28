#pragma once
#include <crow.h>

#include <jsoncons/json.hpp>
#include <optional>

#include "fmt/format.h"
#include "utils/resthelper/resthelper.hpp"
struct Authentication : crow::ILocalMiddleware
{
    struct context
    {
        jsoncons::json credentials;
    };
    Authentication()  = default;
    ~Authentication() = default;

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        try
        {
            ctx.credentials = jsoncons::json::parse(req.body);

            std::optional<std::string> user = ctx.credentials["username"].as<std::string>();
            std::optional<std::string> pass = ctx.credentials["password"].as<std::string>();

            if (!user.has_value())
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Failed to extract username.");
            }
            else if (!pass.has_value())
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Failed to extract password.");
            }
            return;
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, fmt::format("Error (Login Failure) : {}", e.what()));
            return;
        }
    }
    void after_handle(crow::request &req, crow::response &res, context &ctx)
    {
        (void)req;
        (void)res;
        (void)ctx;
    }
};
