#pragma once

#include <crow.h>

#include <functional>
#include <memory>

#include "store/store.hpp"
#include "utils/dosdetector/dosdetector.hpp"
struct RateLimit : crow::ILocalMiddleware
{
   public:
    struct context
    {
    };

    RateLimit() { dos_detector = Store::getObject<DOSDetector>(); }

    void before_handle(crow::request &req, crow::response &res, context &ctx)
    {
        (void)ctx;

        DOSDetector::Status status = dos_detector->is_dos_attack(std::cref(req));

        switch (status)
        {
            case DOSDetector::Status::ALLOWED:
            case DOSDetector::Status::WHITELISTED:
                break;
            case DOSDetector::Status::BLACKLISTED:
                res.code = 403;
                res.end("IP is blacklisted.");
                break;
            case DOSDetector::Status::RATELIMITED:
                res.code = 429;
                res.end("IP is ratelimited.");
                break;
            case DOSDetector::Status::BANNED:
                res.code = 403;
                res.end("IP is banned.");
                break;
            case DOSDetector::Status::ERROR:
                res.code = 500;
                res.end("Server Error");
                break;
            default:
                res.code = 500;
                res.end("Unknown Error");
                break;
        }
    }

    void after_handle(crow::request &req, crow::response &res, context &ctx)
    {
        (void)req;
        (void)res;
        (void)ctx;
    }

   private:
    std::shared_ptr<DOSDetector> dos_detector;
};
