#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

#include "utils/resthelper/resthelper.hpp"

struct PathParameter : crow::ILocalMiddleware
{
   public:
    struct context
    {
        std::unordered_map<std::string, std::string> params;
    };

    PathParameter()          = default;
    virtual ~PathParameter() = default;

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        try
        {
            for (const auto &p : req.url_params.keys())
            {
                std::string key   = p;
                std::string value = req.url_params.get(key);
                ctx.params[key]   = value;
            }
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, "Error parsing path parameter");
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
