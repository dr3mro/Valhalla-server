#pragma once

#include <crow.h>
#include <fmt/core.h>

#include <jsoncons/json.hpp>

#include "utils/resthelper/resthelper.hpp"
#include "xrequest.hpp"
struct Search : crow::ILocalMiddleware
{
    struct context
    {
        jsoncons::json search_json;
    };
    Search()  = default;
    ~Search() = default;
    template <typename XRequestContext>
    void before_handle(crow::request &req, crow::response &res, context &ctx, XRequestContext &xrequest_ctx)
    {
        (void)req;
        ctx.search_json = xrequest_ctx.template get<XRequest>().criteria;

        try
        {
            std::vector<std::string> keys = {"keyword", "filter", "order_by", "direction", "limit", "offset"};

            auto it = keys.begin();

            while (it != keys.end())
            {
                if (!ctx.search_json.contains(*it))
                {
                    RestHelper::errorResponse(res, crow::status::BAD_REQUEST, fmt::format("{} not provided.", *it));
                    return;
                }
                it++;
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