#include "server.hpp"

#include <drogon/drogon.h>
#include <fmt/color.h>
#include <fmt/core.h>

#include "server/extras/banner.hpp"

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
                [](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& callback,
                   drogon::AdviceChainCallback&& chainedcallback)
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

                        callback(resp);
                    }
                    else
                    {
                        chainedcallback();
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
            .setCustom404Page(
                drogon::HttpResponse::newHttpJsonResponse(api::v2::JsonHelper::jsonify("Endpoint not found.")))
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
    std::srand(std::time(0));
    // Select a random color
    int        num_colors   = sizeof(Banner::colors) / sizeof(Banner::colors[0]);
    fmt::color random_color = Banner::colors[arc4random() % num_colors];

    // Clean screen
    // Print Config
    PRINTLINE()
    PRINT("Configuration:", config_.desc, light_blue, yellow)
    PRINTLINE()
    configurator_->printValues();

    // Show ASCII Art
    PRINTLINE()
    PRINT_LOGO(random_color)
    PRINTLINE()
    PRINT(" - Version", GIT_TAG, light_green, yellow)
    PRINT(" - Port", config_.port, light_green, yellow)
    PRINT(" - Threads", config_.threads, light_green, yellow)
    PRINT(" - Database", fmt::format("{} {}", db_config_.max_conn, "connections"), light_green, yellow)
    PRINTLINE()
    std::cout << std::endl;
}
