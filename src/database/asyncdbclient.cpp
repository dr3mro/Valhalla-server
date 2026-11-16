/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file asyncdbclient.cpp
 * @brief Implementation for asyncdbclient: Stores the supplied client, sharing
 * ownership with the caller.
 * @details This file turns the declarations in asyncdbclient.hpp into working
 * code. The constructor simply takes ownership of a ready-made Drogon client;
 * the factory reads the database settings from the Store, asks Drogon to build
 * a PostgreSQL client and sets its execution timeout; the last function formats
 * those settings into the text string that PostgreSQL's client library
 * understands.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the asynchronous database client is actually created. During
 * startup Server::run() calls AsyncDbClient::createClient() when DB_ASYNC is
 * enabled, so this file decides which host, port, database, user, password and
 * SSL mode the server connects with, and how large the connection pool is.
 * Every async query made by the database controllers ultimately goes through
 * the client built here. Without it, the Store would have no client to hand to
 * the controllers and every DB_ASYNC request would report the database as
 * unavailable.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Drogon DbClient: https://drogon.docsforge.com/master/database/
 * - std::shared_ptr (ownership shared between callers):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - std::move (transferring ownership instead of copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - fmt string formatting: https://fmt.dev/
 * - PostgreSQL connection strings:
 * https://www.postgresql.org/docs/current/libpq-connect.html#LIBPQ-CONNSTRING
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Our own declarations; including them first catches mistakes in the header.
#include "database/asyncdbclient.hpp"

// newPgClient() and setTimeout() create and configure the actual client.
#include <drogon/orm/DbClient.h>
// fmt::format builds the connection string from named pieces.
#include <fmt/format.h>

// std::shared_ptr and std::move, used for ownership transfer.
#include <memory>
#include <string>  // std::string, the connection-string type.
// std::move, which avoids an unnecessary reference-count change.
#include <utility>

// Configurator::DatabaseConfig, the settings read by createClient().
#include "configurator/configurator.hpp"
// Store, the service registry holding the one Configurator object.
#include "store/store.hpp"

/**
 * @brief Stores the supplied client, sharing ownership with the caller.
 * @details No validation or configuration happens here; a null client is
 * accepted for tests and the disabled state.
 */
AsyncDbClient::AsyncDbClient(std::shared_ptr<drogon::orm::DbClient> client)
    : client_(std::move(client))
{
}  // Moving means the caller's pointer is emptied and the wrapper becomes an
   // owner without extra bookkeeping.

/**
 * @brief Creates the Drogon Pg client from the Store's database configuration.
 * @details `newPgClient` receives the generated connection string and
 * `DB_MAX_CONN`; the timeout is then set from `DB_ACQUIRE_TIMEOUT_MS`. Any
 * connection-establishment work is left to Drogon.
 */
std::shared_ptr<drogon::orm::DbClient> AsyncDbClient::createClient()
{
  const auto& config =
      Store::getObject<Configurator>()
          ->get<Configurator::DatabaseConfig>();  // Fetch the shared
                                                  // configuration from the
                                                  // Store and read just the
                                                  // DB section; `const&`
                                                  // avoids copying it.
  auto client = drogon::orm::DbClient::
      newPgClient(connectionString(config),
                  config.max_conn);  // Drogon builds the PostgreSQL pool:
                                     // connection text first, maximum number of
                                     // connections second.
  // Drogon's DbClient::setTimeout is an SQL execution timeout: it cancels a
  // running command. It is not a pool-acquire timeout, despite the
  // DB_ACQUIRE_TIMEOUT_MS name; Drogon takes seconds. The setting still comes
  // from the DB_ACQUIRE_TIMEOUT_MS variable, so the name documents intent
  // while the division below adapts the value to what this Drogon API
  // actually measures.
  client->setTimeout(
      static_cast<double>(config.acquire_timeout_ms) /
      1000.0);    // Convert milliseconds to seconds; the cast forces
                  // floating-point division so 1500 ms becomes 1.5 s, not 1 s.
  return client;  // Hand the shared pointer back; the caller wraps and
                  // registers it in the Store.
}

/**
 * @brief Formats the libpq connection string, mapping `config.ssl` to
 * `sslmode`.
 * @details `connect_timeout` is a fixed two seconds, independent of DB_*
 * tuning; host and SSL settings may be overridden by the container image but
 * still arrive through this one configuration object.
 */
std::string AsyncDbClient::connectionString(
    const Configurator::DatabaseConfig& config)
{
  // The container image overrides DB_HOST and DB_SSL; both flow through the
  // same DatabaseConfig. That is why nothing special is done here: whichever
  // source supplied the values, they are already in `config`.
  return fmt::format(
      "host={} port={} dbname={} user={} password={} connect_timeout=2 "
      "sslmode={}",
      config.host,
      config.port,
      config.name,
      config.user,  // Each `{}` is replaced by the next argument, producing
                    // one libpq keyword/value string; note the password is
                    // included in clear text, so
      // never log it.
      config.pass,
      config.ssl ? "require"
                 : "disable");  // The boolean SSL flag becomes the word
                                // PostgreSQL expects; a fixed 2 s connect
                                // timeout keeps startup attempts short.
}
