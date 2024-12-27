#include "server.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <drogon/drogon_callbacks.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <trantor/utils/Logger.h>

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

#include "api/v2/all_routes.hpp"  // IWYU pragma: keep
#include "server/extras/banner.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
#include "utils/message/message.hpp"
#ifndef GIT_TAG
#    define GIT_TAG "unknown"
#endif

int Server::run()
{
    print_banner();

    try
    {
        drogon::app()
            .addListener(std::string(config_.host), config_.port)
            .setThreadNum(config_.threads)
            .setUploadPath(config_.upload_dir)
            .disableSigtermHandling()
            .setLogLevel(static_cast<trantor::Logger::LogLevel>(config_.debug_level))
            .registerPreRoutingAdvice(
                [](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& callback, drogon::AdviceChainCallback&& chainedcallback)
                {
                    if (req->method() == drogon::Options)
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k204NoContent);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                        resp->addHeader("Access-Control-Allow-Credentials", "true");
                        resp->addHeader("Access-Control-Max-Age", "86400");
                        resp->addHeader("X-Powered-By", "Valhalla-Team");

                        std::move(callback)(resp);
                    }
                    else
                    {
                        std::move(chainedcallback)();
                    }
                })
            .registerPostHandlingAdvice(
                [](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& resp)
                {
                    resp->addHeader("Access-Control-Allow-Origin", "*");
                    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                    resp->addHeader("Access-Control-Allow-Credentials", "true");
                    resp->addHeader("Access-Control-Max-Age", "86400");
                    resp->addHeader("X-Powered-By", "Valhalla-Team");
                })
            .setCustom404Page(drogon::HttpResponse::newHttpJsonResponse(api::v2::JsonHelper::jsonify("Endpoint not found.")))
            .run();
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage(fmt::format("Failed to start server on {}:{}.", config_.host, config_.port));
        Message::CriticalMessage(e.what());
        return EXIT_FAILURE;  // Exit with error code
    }

    return EXIT_SUCCESS;  // Exit with success code
}

void Server::print_banner()
{
    // Print Config
    Banner::print_line();
    Banner::print("Configuration:", config_.desc, fmt::color::light_blue, fmt::color::yellow);
    Banner::print_line();
    configurator_->printValues();

    // Show ASCII Art
    Banner::print_line();
    Banner::print_logo(Banner::get_random_color());
    Banner::print_line();
    Banner::print(" - Version", GIT_TAG, fmt::color::light_green, fmt::color::yellow);
    Banner::print(" - Port", config_.port, fmt::color::light_green, fmt::color::yellow);
    Banner::print(" - Threads", config_.threads, fmt::color::light_green, fmt::color::yellow);
    Banner::print(" - Database", fmt::format("{} {}", db_config_.max_conn, "connections"), fmt::color::light_green, fmt::color::yellow);
    Banner::print_line();
}
