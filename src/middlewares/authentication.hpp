#pragma once
#include <crow.h>

#include <jsoncons/json.hpp>
#include <optional>

#include "middlewares/brequest.hpp"
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

            auto user = ctx.credentials["username"].as<std::string>();
            auto pass = ctx.credentials["password"].as<std::string>();

            if (user.empty())
            {
                throw std::runtime_error("Username is empty");
            }
            else if (pass.empty())
            {
                throw std::runtime_error("Password is empty");
            }
            return;
        }
        catch (const std::exception &e)
        {
            res.code = 500;  // login denied
            res.end(fmt::format("Login Failure: {}", e.what()));
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
