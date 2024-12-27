#pragma once
#include <drogon/HttpMiddleware.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/drogon_callbacks.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <trantor/utils/Logger.h>

#include <chrono>
#include <utility>

namespace api::v2::MiddleWares
{
    class ElapsedTime : public drogon::HttpMiddleware<ElapsedTime>
    {
       public:
        ElapsedTime() = default;  // do not omit constructor

        void invoke(const drogon::HttpRequestPtr &req, drogon::MiddlewareNextCallback &&nextCb, drogon::MiddlewareCallback &&mcb) override
        {
            // Store the start time for this request
            std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

            // will run while getting out from this middleware
            std::move(nextCb)(
                [mcb = std::move(mcb), start_time, req](const drogon::HttpResponsePtr &resp)
                {
                    // Calculate the elapsed time for this request
                    auto end_time   = std::chrono::steady_clock::now();
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                    // Log the elapsed time
                    LOG_INFO << fmt::format(": {}{}{}{}{}", fmt::format(fmt::fg(fmt::color::orange), fmt::format("[{} ms] ", elapsed_ms)),
                        fmt::format(fmt::fg(fmt::color::yellow), fmt::format("[{}]", req->getPeerAddr().toIp())),
                        fmt::format(fmt::fg(fmt::color::red), fmt::format("[{}]", req->getMethodString())),
                        fmt::format(fmt::fg(fmt::color::magenta), fmt::format("[{}]", static_cast<int>(resp->getStatusCode()))),
                        fmt::format(fmt::fg(fmt::color::cyan), fmt::format("[{}]", req->path())));
                    std::move(mcb)(resp);
                });
        }
    };
}  // namespace api::v2::MiddleWares
