#pragma once

#include <crow.h>
#include <xxhash.h>

#include <jsoncons/json.hpp>

#include "fmt/format.h"
#include "middlewares/brequest.hpp"

struct DataIntegrity : crow::ILocalMiddleware
{
    struct context
    {
    };
    DataIntegrity()  = default;
    ~DataIntegrity() = default;

    template <typename BRequestContext>
    void before_handle(crow::request &req, crow::response &res, context &ctx, BRequestContext &brequest_ctx)
    {
        (void)ctx;

        if (!(req.headers.contains("Enable-data-integrity-check") && req.get_header_value("Enable-data-integrity-check") == "true"))
            return;

        try
        {
            jsoncons::json &json = brequest_ctx.template get<BRequest>().criteria;

            if (!json.contains("xxh64sum") || !json.contains("payload"))
            {
                res.code = 400;
                res.end("xxh64sum or payload not provided, aborting.");
                return;
            }

            std::string  checksum = json.at("xxh64sum").as<std::string>();
            std::string  payload  = json.at("payload").as<std::string>();
            XXH64_hash_t hash     = XXH64(payload.data(), payload.size(), 0);

            if (checksum != fmt::format("{:016x}", hash))
            {
                res.code = 400;
                res.end("hash mismatch, aborting.");
                return;
            }
        }
        catch (const std::exception &e)
        {
            res.code = 500;
            res.end(fmt::format("Failure, {}", e.what()));
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