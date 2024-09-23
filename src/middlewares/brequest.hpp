#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

#include "utils/resthelper/resthelper.hpp"

struct BRequest : crow::ILocalMiddleware
{
   public:
    struct context
    {
        jsoncons::json criteria;
    };

    BRequest()  = default;
    ~BRequest() = default;

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        try
        {
            ctx.criteria = jsoncons::json::parse(req.body);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, "Error parsing request body");
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
