// #pragma once

// #include <crow.h>

// #include <functional>
// #include <memory>

// #include "store/store.hpp"
// #include "utils/dosdetector/dosdetector.hpp"
// #include "utils/resthelper/resthelper.hpp"
// struct RateLimit : crow::ILocalMiddleware
// {
//    public:
//     struct context
//     {
//     };

//     RateLimit() { dos_detector = Store::getObject<DOSDetector>(); }

//     void before_handle(crow::request &req, crow::response &res, context &ctx)
//     {
//         (void)ctx;

//         DOSDetector::Status status = dos_detector->is_dos_attack(req);

//         switch (status)
//         {
//             case DOSDetector::Status::ALLOWED:
//             case DOSDetector::Status::WHITELISTED:
//                 break;
//             case DOSDetector::Status::BLACKLISTED:
//                 RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "IP is blacklisted.");
//                 break;
//             case DOSDetector::Status::RATELIMITED:
//                 RestHelper::errorResponse(res, crow::status::TOO_MANY_REQUESTS, "IP is ratelimited.");
//                 break;
//             case DOSDetector::Status::BANNED:
//                 RestHelper::errorResponse(res, crow::status::SERVICE_UNAVAILABLE, "IP is banned.");
//                 break;
//             case DOSDetector::Status::ERROR:
//                 RestHelper::failureResponse(res, "Server error.");
//                 break;
//             default:
//                 RestHelper::failureResponse(res, "Unknown error.");
//                 break;
//         }
//     }

//     void after_handle(crow::request &req, crow::response &res, context &ctx)
//     {
//         (void)req;
//         (void)res;
//         (void)ctx;
//     }

//    private:
//     std::shared_ptr<DOSDetector> dos_detector;
// };
