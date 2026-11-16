/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file server.hpp
 * @brief Declaration of Server, the application-level owner of Drogon setup and
 * startup/shutdown sequencing.
 *
 * @details This header declares the Server class. The class gathers everything
 * needed to start the application: a snapshot of the configuration, the shared
 * singletons, and the small filter/middleware objects used by the two global
 * request hooks. Its constructor caches those objects; run() performs the
 * actual Drogon setup, blocks in the event loop, and then shuts the worker
 * pools down in a safe order.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Server is the single coordinator between the tiny main() function and the
 * rest of the application. main() only creates a Server and calls run();
 * everything else — printing the banner, choosing the listening port and thread
 * count, adding CORS and method checks to requests, and draining the database
 * and password workers on exit — is either done here or started from here.
 * Without this header there would be no agreed interface for main() to call,
 * and the startup/shutdown ordering rules would have nowhere to live.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Smart pointers and ownership (shared_ptr):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - RAII (resources tied to object lifetime):
 * https://en.cppreference.com/w/cpp/language/raii
 * - The Rule of Five (copy/move special members):
 * https://en.cppreference.com/w/cpp/language/rule_of_three
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Ask the compiler to include this file at most once per source
              // file, avoiding duplicate definitions.

// for the Drogon response types used by the framework wiring
#include <drogon/HttpResponse.h>
// for drogon::HttpMethod, the enum used by isConventionalMethod()
#include <drogon/HttpTypes.h>

// for std::shared_ptr, the smart pointer type of the member objects
#include <memory>

// declares the authentication filter type held as a member
#include "api/v2/filters/auth.hpp"
// declares the rate-limit / DoS-protection filter type held as a member
#include "api/v2/filters/ratelimit.hpp"
// declares the request-timing middleware type held as a member
#include "api/v2/middlewares/elapsedtime.hpp"
// declares Configurator and its ServerConfig/DatabaseConfig structs
#include "configurator/configurator.hpp"

class Logger;  // Forward declaration: the class exists elsewhere, and a pointer
               // to it is all this header needs.
class DatabaseSchema;  // Forward declaration: avoids pulling the whole schema
                       // header into this one.

/**
 * @class Server
 * @brief Owns application startup: configuration, banner, Drogon wiring, and
 * post-run executor shutdown.
 *
 * @details The constructor resolves long-lived singletons from the Store and
 * copies the server and database configuration by value, so later changes to
 * the Configurator are not seen through those copies. run() configures and
 * starts the Drogon application, installs the pre-routing and pre-sending
 * advice (method filtering and CORS), and blocks until the event loops stop.
 * Only then does it shut the worker pools down, while the Store singletons they
 * depend on are still alive. The filter and middleware members are created
 *          eagerly so one instance is reused. Registration of the included
 * Drogon components (including the route headers brought in by all_routes.hpp)
 * is handled by Drogon itself, outside this class.
 * @note Not copyable. Move construction is defaulted; move assignment is
 * deleted.
 * @see Cors, Store, Configurator
 */
class Server  // The one object that drives startup and an orderly shutdown of
              // the whole server.
{
 public:  // Members below are callable from outside the class (mainly by
          // main()).
  /**
   * @brief Resolves the shared singletons and caches copies of the
   * server/database configuration.
   *
   * @details Objects come from Store::getObject, so a later registration
   * through Store::replaceObject will not replace the instances held here.
   * The filter and middleware instances are constructed here and owned by the
   * Server for its entire lifetime.
   */
  Server();  // Grab the shared objects from the Store and copy the current
             // config values into this object.

  /**
   * Virtual destructor; the Server owns only shared_ptrs, so default
   * destruction is sufficient.
   */
  virtual ~Server() = default;  // Virtual so deletion through a base pointer
                                // is safe; no custom cleanup is needed.
  /**
   * Copy construction is not supported.
   */
  Server(const Server&) =
      delete;  // Copies are forbidden: two Servers would both
               // try to control the single global framework.
  /**
   * Copy assignment is not supported.
   */
  Server& operator=(const Server&) =
      delete;  // Assignment is forbidden for the same reason as copying.
  /**
   * Move construction is defaulted.
   */
  Server(Server&&) = default;  // Moving is allowed because the members are
                               // smart pointers that simply transfer ownership.
  /**
   * Move assignment is not supported.
   */
  Server& operator=(Server&&) =
      delete;  // Move assignment is disabled to keep the object's
               // lifetime simple and predictable.

  /**
   * @brief Prints the banner, configures and runs Drogon, then drains the
   * worker pools.
   *
   * @details Blocks in Drogon's run() until the event loops stop. On the way
   * out it shuts down DbExecutor first and PasswordWorker second, because
   * database tasks may hand work to the password worker. Any std::exception
   *          from setup or teardown is reported through Message and turned
   * into EXIT_FAILURE.
   * @return EXIT_SUCCESS after a clean run and shutdown, EXIT_FAILURE on a
   * reported exception.
   * @note SIGTERM handling is disabled (see the .disableSigtermHandling()
   * call in the implementation), so shutdown is driven by the framework event
   * loops stopping rather than by Drogon's own signal handler.
   * @see print_banner(), Store, DbExecutor, PasswordWorker
   */
  int run();  // Start the web framework, block while it serves requests, then
              // shut the pools down and return an exit code.

 private:  // Members below are internal helpers and state used only by the
           // class itself.
  /**
   * @brief Prints the effective configuration and the ASCII logo to stdout.
   *
   * @details The configuration dump is produced by
   * Configurator::printValues() and is followed by version, port, thread
   * count, database connection count, and documentation URL. stdout is
   * explicitly flushed so the banner is visible before the Drogon loggers
   * take over the console.
   */
  void print_banner();  // Write the settings and the logo before framework
                        // logging starts, then flush.

  /**
   * @brief Reports whether an HTTP method is one this server accepts.
   *
   * @details Used by the pre-routing advice to reject uncommon methods with
   * 405 before routing; accepted methods continue down the chain and are also
   * advertised in the Allow / CORS headers.
   * @param[in] method Method of the incoming request.
   * @return true for GET, POST, PUT, DELETE, OPTIONS and HEAD; false
   * otherwise (including PATCH).
   */
  static bool isConventionalMethod(
      drogon::HttpMethod method);  // Static because it needs no object state:
                                   // it only looks at the method value.

  /**
   * Shared Logger instance used by the application; held here for the
   * lifetime of the Server.
   */
  std::shared_ptr<Logger>
      logger_;  // A shared logger so every component logs the same way; a
                // shared_ptr keeps it alive.
  /**
   * Source of the configuration values copied into config_ and db_config_.
   */
  std::shared_ptr<Configurator>
      configurator_;  // Holds the original settings object so printValues()
                      // can dump the live values.
  /**
   * Snapshot of the server settings used to configure the listener and to
   * print the banner.
   */
  Configurator::ServerConfig
      config_;  // A copy of host/port/threads/etc.; copied so later config
                // changes cannot alter it mid-run.
  /**
   * Snapshot of the database settings; its async flag selects the
   * asynchronous DB client at startup.
   */
  Configurator::DatabaseConfig
      db_config_;  // A copy of the DB settings; the async flag decides which
                   // client is registered.
  /**
   * Shared database-schema validator, kept alive for the lifetime of the
   * process.
   */
  std::shared_ptr<DatabaseSchema>
      databaseSchema_;  // Shared validator kept alive here so request
                        // handling can use it at any time.
  /**
   * Authentication filter instance. Constructed here but unused: Drogon
   * resolves filters by class name from the route macros and instantiates its
   * own singleton via reflection.
   */
  std::shared_ptr<api::v2::Filters::Auth>
      auth_filter_;  // Kept as a member even though Drogon builds its own
                     // filter via reflection.
  /**
   * Request-timing middleware instance. Constructed here but unused for the
   * same reason as auth_filter_.
   */
  std::shared_ptr<api::v2::MiddleWares::ElapsedTime>
      elapsed_time_;  // Same story: a member instance exists, but Drogon
                      // creates the one it uses.
  /**
   * Rate-limit / DoS-protection filter instance. Constructed here but unused
   * for the same reason.
   */
  std::shared_ptr<api::v2::Filters::RateLimit>
      rate_limit_;  // Same story: kept for ownership clarity; Drogon
                    // instantiates its own copy.
};
