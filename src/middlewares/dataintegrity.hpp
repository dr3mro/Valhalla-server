#pragma once

#include <crow.h>
#include <xxhash.h>

#include <jsoncons/json.hpp>

#include "fmt/format.h"
#include "middlewares/brequest.hpp"
#include "utils/resthelper/resthelper.hpp"

struct DataIntegrity : crow::ILocalMiddleware
{
    struct context
    {
    };
    DataIntegrity()  = default;
    ~DataIntegrity() = default;

    template <typename AllContext>
    void before_handle(crow::request &req, crow::response &res, context &ctx, AllContext &all_ctx)
    {
        (void)ctx;

        if (!(req.headers.contains("Enable-data-integrity-check") && req.get_header_value("Enable-data-integrity-check") == "true"))
            return;

        try
        {
            jsoncons::json &json = all_ctx.template get<BRequest>().criteria;

            if (!json.contains("xxh64sum") || !json.contains("payload"))
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "xxh64sum or payload not provided, aborting.");
                return;
            }

            std::string  checksum = json.at("xxh64sum").as<std::string>();
            std::string  payload  = json.at("payload").as<std::string>();
            XXH64_hash_t hash     = XXH64(payload.data(), payload.size(), 0);

            if (checksum != fmt::format("{:016x}", hash))
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "hash mismatch, aborting.");
                return;
            }
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