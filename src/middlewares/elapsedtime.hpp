#pragma once
#include <drogon/drogon.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <chrono>

namespace api
{
    namespace v2
    {
        namespace MiddleWares
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
                    nextCb(
                        [mcb = std::move(mcb), start_time, req](const drogon::HttpResponsePtr &resp)
                        {
                            // Calculate the elapsed time for this request
                            auto end_time   = std::chrono::steady_clock::now();
                            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                            // Log the elapsed time
                            LOG_INFO << fmt::format(": {}{}{}{}", fmt::format(fmt::fg(fmt::color::orange), std::format("[{} ms] ", elapsed_ms)),
                                                    fmt::format(fmt::fg(fmt::color::yellow), std::format("[{}]", req->getPeerAddr().toIp())),
                                                    fmt::format(fmt::fg(fmt::color::red), std::format("[{}]", req->getMethodString())),
                                                    fmt::format(fmt::fg(fmt::color::cyan), std::format("[{}]", req->path())));
                            mcb(resp);
                        });
                }
            };
        }  // namespace MiddleWares
    }  // namespace v2
}  // namespace api