/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file server.cpp
 * @brief Implementation of Server: Drogon configuration, request hooks, and
 * shutdown sequencing.
 *
 * @details This file turns the Server declaration into working code. It builds
 * the Drogon application — listener, thread count, log level, a custom 404 page
 * — and registers two global hooks: one that answers CORS preflights and
 * rejects unusual HTTP methods before routing, and one that adds CORS headers
 * to every response. It also prints the startup banner and, after the event
 * loops stop, drains the database and password worker pools in a safe order.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the file that actually boots the web server. main() calls
 * Server::run(), and the code here is what opens the listening port and starts
 * Drogon's event loops so HTTP requests can be served. It also contains the two
 * cross-cutting request rules (CORS and allowed methods) that apply to every
 * endpoint, so individual controllers do not have to repeat them. Finally, it
 * owns the shutdown order; without it, worker threads could be destroyed while
 * they still had queued work, causing crashes at exit.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Lambda expressions (the inline anonymous functions used as callbacks):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - Move semantics and std::move:
 * https://en.cppreference.com/w/cpp/utility/move
 * - Drogon's application and advice hooks:
 * https://github.com/drogonframework/drogon
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include "server.hpp"  // This class's own declaration is always included first.

// for drogon::app(), the single global framework object we configure and run
#include <drogon/HttpAppFramework.h>
// for drogon::HttpRequestPtr, used by the two advice hooks below
#include <drogon/HttpRequest.h>
// for drogon::HttpResponse and the newHttpResponse/newHttpJsonResponse
// factories
#include <drogon/HttpResponse.h>
// for status constants such as k204NoContent and k405MethodNotAllowed
#include <drogon/HttpTypes.h>
// an umbrella header that pulls in the rest of the Drogon framework
#include <drogon/drogon.h>
// for AdviceCallback and AdviceChainCallback used by pre-routing advice
#include <drogon/drogon_callbacks.h>
// for fmt::color values used when printing the coloured banner
#include <fmt/color.h>
// for fmt::format and fmt::print used by messages and the banner
#include <fmt/core.h>
#include <fmt/format.h>  // for the remaining fmt formatting helpers
// for trantor::Logger::LogLevel used when setting the framework log level
#include <trantor/utils/Logger.h>

#include <cstdlib>  // for EXIT_SUCCESS and EXIT_FAILURE, the exit status codes
// for std::exception, the type caught around startup and shutdown
#include <exception>
#include <iostream>  // for std::cout and std::flush used to print the banner
// for std::make_shared, used to create the filter and middleware objects
#include <memory>
// for std::string and std::to_string used throughout this file
#include <string>
// for std::move, used to invoke the advice callbacks exactly once
#include <utility>

// Including this header makes Drogon discover and register all of the
// application's HTTP routes.
#include "api/v2/all_routes.hpp"  // IWYU pragma: keep
// the timing middleware used by the pre-sending hook
#include "api/v2/middlewares/elapsedtime.hpp"
// Configurator types used to read the cached settings
#include "configurator/configurator.hpp"
// AsyncDbClient, created and registered when async DB mode is enabled
#include "database/asyncdbclient.hpp"
// DbExecutor, whose task queue is drained during shutdown
#include "database/dbexecutor.hpp"
// PasswordWorker, drained after the DB executor
#include "gatekeeper/passwordcrypt/passwordworker.hpp"
// Cors::apply(), which adds CORS headers in the pre-sending hook
#include "server/cors.hpp"
// Banner helpers used to print the startup banner
#include "server/extras/banner.hpp"
// Store, the global registry of shared singleton objects
#include "store/store.hpp"
#include "utils/Logger/logger.hpp"  // the Logger type resolved from the Store
// JsonHelper::jsonify(), which builds the custom 404 JSON body
#include "utils/jsonhelper/jsonhelper.hpp"
// Message helpers used to report startup failures
#include "utils/message/message.hpp"
// the DatabaseSchema type resolved from the Store
#include "validator/databaseschema/databaseschema.hpp"

/**
 * @brief Resolves the Store singletons and snapshots the configuration used
 * throughout startup.
 *
 * @details Objects fetched here are shared_ptrs, so they remain valid for the
 * lifetime of the Server. The two Configurator structs are copied by value;
 * configuration changes made after construction are not observed.
 */
Server::Server()  // Initialise every member: fetch shared singletons from the
                  // Store and snapshot the configuration.
    : logger_(Store::getObject<Logger>()),  // Fetch (or create) the shared
                                            // logger from the global Store.
      configurator_(
          Store::getObject<Configurator>()),  // Fetch the shared settings
                                              // object, also used later for
                                              // printValues() and CORS.
      config_(configurator_->get<
          Configurator::ServerConfig>()),  // Copy the server settings so later
                                           // changes cannot alter this run.
      db_config_(configurator_->get<
          Configurator::DatabaseConfig>()),  // Copy the DB settings, including
                                             // the flag that selects async
                                             // mode.
      databaseSchema_(
          Store::getObject<DatabaseSchema>()),  // Fetch the shared schema
                                                // validator used by request
                                                // handling.
      auth_filter_(std::make_shared<
          api::v2::Filters::Auth>()),  // Create one authentication filter
                                       // instance owned by this Server.
      elapsed_time_(std::make_shared<
          api::v2::MiddleWares::ElapsedTime>()),  // Create one request-timing
                                                  // middleware instance.
      rate_limit_(std::make_shared<
          api::v2::Filters::RateLimit>())  // Create one rate-limit filter
                                           // instance (last, so no trailing
                                           // comma).
{
}
/**
 * @brief Configures and runs the Drogon application, then drains the worker
 * pools.
 *
 * @details Registers the listener, thread count, upload path, log level, a
 * custom 404 page and two global advice hooks: the pre-routing hook
 * short-circuits OPTIONS and rejects uncommon methods, and the pre-sending hook
 * applies CORS headers to every response. When db_config_.async is set, an
 * AsyncDbClient is registered in the Store before the event loops start.
 * Control returns from drogon::app().run() only after the event loops stop.
 * @return EXIT_SUCCESS on a clean run and shutdown, EXIT_FAILURE if setup or
 * shutdown throws std::exception.
 * @note The post-run shutdown order is deliberate: DbExecutor is drained before
 * PasswordWorker because database tasks may enqueue password work. Both happen
 * here, while the Store singletons still exist, rather than in static
 *       destructors.
 * @see print_banner(), Cors::apply(), Store::getObject()
 */
int Server::run()  // Configure Drogon, serve requests, then drain the worker
                   // pools and return an exit code.
{
  try  // Catch any exception from setup or teardown so it can be reported
       // cleanly.
  {
    print_banner();  // Show the effective settings and the logo before the
                     // framework starts logging.
    if (db_config_.async)  // Only asynchronous DB mode needs a separate
                           // client registered up front.
    {
      Store::registerObject<AsyncDbClient>(
          AsyncDbClient::createClient());  // Build an async DB client and
                                           // put it in the Store for
                                           // controllers to use.
    }
    drogon::app()  // Get the single global Drogon application; the chain below
                   // configures it before run() is called.
        .addListener(std::string(config_.host),
            config_.port)                   // Listen on the configured host
                                            // address and TCP port.
        .setThreadNum(config_.threads)      // Use the configured number of
                                            // I/O worker threads.
        .setUploadPath(config_.upload_dir)  // Tell Drogon where to
                                            // store uploaded files.
        .disableSigtermHandling()           // Let the event loops drive
                                            // shutdown instead of
                                            // Drogon's own SIGTERM
                                            // handler.
        .setLogLevel(static_cast<trantor::Logger::LogLevel>(
            config_.debug_level))   // Convert the numeric config
                                    // value into the framework's
                                    // log-level enum.
        .registerPreRoutingAdvice(  // Install a hook that runs
                                    // before routing: answer
                                    // preflights and reject odd
                                    // methods.
            [](const drogon::HttpRequestPtr& req,
                drogon::AdviceCallback&&     callback,
                drogon::AdviceChainCallback&&
                    chainedcallback)  // Request + callbacks.
            {
              // CORS preflights are answered once, here, with
              // 204 and no body; they never reach a
              // controller.
              if (req->method() == drogon::Options)  // A browser's preflight
                                                     // check uses OPTIONS and
                                                     // expects a quick empty
                                                     // reply.
              {
                auto resp =
                    drogon::HttpResponse::newHttpResponse();  // Create a fresh,
                                                              // empty response
                                                              // object.
                resp->setStatusCode(drogon::k204NoContent);   // 204 means
                                                              // "success, no
                                                              // content",
                                                              // which is
                                                              // correct for a
                                                              // preflight.
                std::move(callback)(resp);  // Send this response immediately
                                            // and do not continue the chain.
              }
              // Uncommon methods are rejected before routing;
              // Allow advertises what is accepted.
              else if (!isConventionalMethod(req->method()))  // If the method
                                                              // is unsupported,
                                                              // stop before any
                                                              // route is
                                                              // matched.
              {
                auto resp =
                    drogon::HttpResponse::newHttpResponse();  // Create a
                                                              // response object
                                                              // to carry the
                                                              // error status.
                resp->setStatusCode(
                    // 405 tells the client the method is not
                    // allowed.
                    drogon::k405MethodNotAllowed);
                resp->addHeader("Allow",
                    Cors::allowedMethods());  // Advertise
                                              // which
                                              // methods the
                                              // server does
                                              // accept.
                std::move(callback)(resp);    // Respond now; the request never
                                              // reaches a controller.
              }
              else  // The method is acceptable, so let normal
                    // routing continue.
              {
                std::move(chainedcallback)();  // Call the chain
                                               // callback to pass
                                               // the request to
                                               // the next step.
              }
            })
        // The only place CORS headers are written: runs once per
        // response, after every filter and pre-routing reply.
        .registerPreSendingAdvice([](const drogon::HttpRequestPtr&   req,
                                      const drogon::HttpResponsePtr& resp)
            { Cors::apply(req, resp); })  // CORS headers.
        .setCustom404Page(drogon::HttpResponse::newHttpJsonResponse(
            api::v2::JsonHelper::jsonify(
                "Endpoint not found.")))  // JSON body for
                                          // unknown routes.
        .run();  // Start the event loops and block here until
                 // they stop; execution continues below
                 // afterwards.

    // The event loops have stopped. Finish the queued database and password
    // work and join the workers now, while the connection pool and the
    // other singletons still exist, instead of leaving it to static
    // destruction. The database executor goes first because its tasks may
    // hand work to the password worker.
    Store::getObject<DbExecutor>()
        ->shutdown();  // Drain the DB task queue and stop its threads
                       // before the DB singletons disappear.
    Store::getObject<PasswordWorker>()
        ->shutdown();  // Then drain password-hashing work, which DB tasks
                       // may have queued.
  }
  catch (const std::exception& e)  // Reached if any of the setup or teardown
                                   // above threw an exception.
  {
    Message::ErrorMessage(
        fmt::format("Failed to start server on {}:{}.", config_.host,
            config_.port));  // Report which address failed to start.
    Message::CriticalMessage(e.what());  // Include the exception's own
                                         // description as a critical message.
    return EXIT_FAILURE;  // Signal failure to main(), and from there to the
                          // operating system.
  }
  return EXIT_SUCCESS;  // The server ran and shut down cleanly.
}
/**
 * @brief Prints the effective configuration and the startup logo.
 *
 * @details Writes to stdout and explicitly flushes it so the banner appears
 * before framework logging begins. The displayed version comes from the
 * build-time VALHALLA_GIT_TAG, not from the Configurator snapshot.
 */
void Server::print_banner()  // Print the configuration summary and the ASCII
                             // logo, then flush stdout.
{
  // Print Config
  Banner::print_line();  // Draw a separator line before the configuration
                         // block.
  Banner::print("Configuration:", config_.desc, fmt::color::light_blue,
      fmt::color::yellow);  // Print the section title with coloured label and
                            // value.
  Banner::print_line();     // Draw a separator after the title.
  configurator_->printValues();  // Dump every effective configuration value
                                 // from the source object.

  // Show ASCII Art
  Banner::print_line();  // Draw a separator before the logo section.
  Banner::print_logo(Banner::get_random_color());  // Print the logo in a
                                                   // randomly chosen colour.
  Banner::print_line();  // Draw a separator after the logo.
  Banner::print(" - Version", VALHALLA_GIT_TAG, fmt::color::light_green,
      fmt::color::yellow);  // Show the build-time Git tag as the version.
  Banner::print(" - Port", config_.port, fmt::color::light_green,
      fmt::color::yellow);  // Show which TCP port the server listens on.
  Banner::print(" - Threads", config_.threads, fmt::color::light_green,
      fmt::color::yellow);  // Show how many worker threads were configured.
  Banner::print(" - Database",
      fmt::format("{} {}", db_config_.max_conn, "connections"),
      fmt::color::light_green, fmt::color::yellow);  // Max DB connections.
  Banner::print(" - Doc",
      "http://localhost:" + std::to_string(config_.port) + "/api/v2/doc",
      fmt::color::light_green, fmt::color::yellow);  // Docs URL.
  Banner::print_line();  // Close the banner with a final separator line.

  std::flush(std::cout);  // Force the banner out before Drogon's loggers
                          // start writing to the same console.
}

/**
 * Reports whether a method should be routed rather than rejected in
 * pre-routing.
 *
 * method: Method of the incoming request.
 * Returns true for GET, POST, PUT, DELETE, OPTIONS and HEAD; false for all
 * others, notably PATCH. Note: PATCH is advertised by Cors::allowedMethods()
 * but is not accepted here; the two lists differ in the current implementation.
 */
bool Server::isConventionalMethod(
    drogon::HttpMethod method)  // Return true only for the six methods this
                                // server is willing to route.
{
  return method == drogon::Get || method == drogon::Post ||
         method == drogon::Put || method == drogon::Delete ||
         method == drogon::Options ||  // Accepted.
         method ==
             drogon::Head;  // Also accept HEAD (like GET but without a body);
                            // everything else, such as PATCH, returns false.
}
