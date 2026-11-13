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

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "api/v2/all_routes.hpp"  // IWYU pragma: keep
#include "api/v2/middlewares/elapsedtime.hpp"
#include "configurator/configurator.hpp"
#include "server/extras/banner.hpp"
#include "store/store.hpp"
#include "utils/Logger/logger.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
#include "utils/message/message.hpp"
#include "validator/databaseschema/databaseschema.hpp"
#ifndef GIT_TAG
#    define GIT_TAG "unknown"
#endif
namespace
{
    // Drogon's HttpMethod enum has no TRACE/CONNECT entry at all, so a client sending one of those
    // (or anything else this API never registers, like PATCH) parses to one of the "exotic" values
    // below. Drogon's router still auto-generates a 405 for that on a path that otherwise exists,
    // but it does so before registerPostHandlingAdvice *or* registerHttpResponseCreationAdvice ever
    // see the response - both already tried and failed to add an Allow header to it (see the long
    // comment on registerHttpResponseCreationAdvice below). The only hook early enough is
    // registerPreRoutingAdvice, intercepting before Drogon's own router runs at all.
    bool isConventionalMethod(drogon::HttpMethod method)
    {
        return method == drogon::Get || method == drogon::Post || method == drogon::Put || method == drogon::Delete || method == drogon::Options ||
               method == drogon::Head;
    }

    // Path patterns this app actually registered (from getHandlersInfo(), populated once all
    // controllers are built - see registerBeginningAdvice below), turned into matching regexes so a
    // live request's path with a real id/segment in a "{param}" slot still matches the pattern it
    // was registered under. Used only to tell "this path exists, just not for this method" apart
    // from a path that plain doesn't exist, which should still 404 rather than 405.
    std::vector<std::regex> g_knownPathPatterns;
}  // namespace

Server::Server()
    : logger_(Store::getObject<Logger>()),
      configurator_(Store::getObject<Configurator>()),
      config_(configurator_->get<Configurator::ServerConfig>()),
      db_config_(configurator_->get<Configurator::DatabaseConfig>()),
      databaseSchema_(Store::getObject<DatabaseSchema>()),
      auth_filter_(std::make_shared<api::v2::Filters::Auth>()),
      elapsed_time_(std::make_shared<api::v2::MiddleWares::ElapsedTime>()),
      rate_limit_(std::make_shared<api::v2::Filters::RateLimit>())
{
}
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
            .registerBeginningAdvice(
                []()
                {
                    // Runs once, after every controller/filter/plugin has registered its routes, so
                    // getHandlersInfo() here returns the app's complete, final route table - this is
                    // the only point that's guaranteed. Build the known-path-pattern list once from
                    // it; registerPreRoutingAdvice below consults it on every request.
                    for (const auto& info : drogon::app().getHandlersInfo())
                    {
                        const std::string& pattern   = std::get<0>(info);
                        std::string        regex_src = "^" + std::regex_replace(pattern, std::regex(R"(\{[^}]*\})"), "[^/]+") + "$";
                        g_knownPathPatterns.emplace_back(regex_src);
                    }
                })
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
                    else if (!isConventionalMethod(req->method()) &&
                             std::ranges::any_of(
                                 g_knownPathPatterns, [path = std::string(req->path())](const std::regex& re) { return std::regex_match(path, re); }))
                    {
                        // A method this API never registers for ANY path (TRACE -> HttpMethod::Invalid,
                        // PATCH, PROPFIND, ...) sent to a path that otherwise exists. Drogon's own router
                        // would auto-generate a 405 for this, but neither registerPostHandlingAdvice (never
                        // fires - no handler ran) nor registerHttpResponseCreationAdvice (fires too early to
                        // see the final status code) can add the Allow header RFC 9110 7.6.1 requires to it -
                        // both tried, both confirmed not to work by repeated schemathesis runs. Answer it
                        // ourselves instead, before Drogon's router ever runs.
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::k405MethodNotAllowed);
                        resp->addHeader("Access-Control-Allow-Origin", "*");
                        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                        resp->addHeader("Access-Control-Allow-Credentials", "true");
                        resp->addHeader("Access-Control-Max-Age", "86400");
                        resp->addHeader("X-Powered-By", "Valhalla-Team");
                        resp->addHeader("Allow", "GET, POST, PUT, DELETE, OPTIONS");

                        std::move(callback)(resp);
                    }
                    else
                    {
                        std::move(chainedcallback)();
                    }
                })
            // Every response Drogon creates (handler-made or internal: 404s, OPTIONS/TRACE responses
            // crafted above, etc.) passes through here, so the shared CORS headers land everywhere.
            .registerHttpResponseCreationAdvice(
                [](const drogon::HttpResponsePtr& resp)
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
    Banner::print(" - Doc", "http://localhost:" + std::to_string(config_.port) + "/api/v2/doc", fmt::color::light_green, fmt::color::yellow);
    Banner::print_line();
    std::flush(std::cout);
}
