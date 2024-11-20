#include "server2.hpp"

#include <drogon/drogon.h>
#include <fmt/color.h>
#include <fmt/core.h>

#include "server/extras/banner.hpp"

#ifndef GIT_TAG
#    define GIT_TAG "unknown"
#endif

int Server2::run()
{
    print_banner();

    try
    {
        drogon::app()
            .addListener(config_.host.data(), config_.port)
            //.setLogLevel(static_cast<trantor::Logger::LogLevel>(config_.debug_level))
            .setThreadNum(config_.threads)
            .registerPreRoutingAdvice(
                [](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& cb, drogon::AdviceChainCallback&& ccb)
                {
                    if (req->method() == drogon::Options)
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k204NoContent);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                        resp->addHeader("Access-Control-Allow-Credentials", "true");
                        cb(resp);
                    }
                    else
                    {
                        ccb();
                    }
                })
            .disableSigtermHandling()
            .registerPostHandlingAdvice(
                [](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& resp)
                {
                    resp->addHeader("Access-Control-Allow-Origin", "*");
                    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                    resp->addHeader("Access-Control-Allow-Credentials", "true");
                })
            .setCustom404Page(drogon::HttpResponse::newHttpJsonResponse(Helper::jsonify("Not Implemented yet!")))
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

void Server2::print_banner()
{
    std::srand(std::time(0));
    // Select a random color
    int        num_colors   = sizeof(Banner::colors) / sizeof(Banner::colors[0]);
    fmt::color random_color = Banner::colors[std::rand() % num_colors];

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
