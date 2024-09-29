#pragma once
#include <crow.h>

#include <optional>

#include "jsoncons/basic_json.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

struct Deauthentication : crow::ILocalMiddleware
{
    struct context
    {
        std::optional<std::string> token;
    };
    Deauthentication()  = default;
    ~Deauthentication() = default;

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        try
        {
            std::optional<std::string> raw_token = jsoncons::json::parse(req.body).at("token").as<std::string>();

            if (!raw_token)
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "logout token not provided.");
                return;
            }

            if (raw_token.value().substr(0, 7) == "Bearer ")
            {
                ctx.token = raw_token.value().substr(7);
            }

            if (!ctx.token)
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "token parsing failure.");
            }

            return;
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
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
