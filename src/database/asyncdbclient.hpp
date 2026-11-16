/**
 * @file asyncdbclient.hpp
 * @brief Store-registered holder for the Drogon PostgreSQL client used when
 * DB_ASYNC=true.
 * @details This header defines one small class that owns a Drogon database
 * client. The client is kept in a shared pointer so several parts of the server
 * can refer to the same underlying connection pool. The class also contains two
 * helpers: one that builds the client from the database settings, and one that
 * writes those settings into the keyword/value string PostgreSQL expects.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This file is the home of the asynchronous (non-blocking) database client.
 * When the DB_ASYNC setting is on, the server startup code (Server::run())
 * calls AsyncDbClient::createClient() and stores the result in the Store, which
 * is the server's shared service registry. Later, each database controller
 * method fetches this object from the Store and passes the Drogon client inside
 * it to asyncdb::execSqlCoro(), which runs the SQL without occupying a worker
 * thread. Tests can register a fake (or empty) client through
 * Store::replaceObject(). Without this file there would be no way for the async
 * controllers to reach PostgreSQL, and the whole DB_ASYNC code path would be
 * unreachable; the older synchronous connection pool would remain the only
 * working option.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - C++20 coroutines: https://en.cppreference.com/w/cpp/language/coroutines
 * - Drogon DbClient: https://drogon.docsforge.com/master/database/
 * - std::shared_ptr (shared ownership of one object):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - RAII (tying a resource's lifetime to an object's lifetime):
 * https://en.cppreference.com/w/cpp/language/raii
 * - PostgreSQL connection strings:
 * https://www.postgresql.org/docs/current/libpq-connect.html#LIBPQ-CONNSTRING
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file, so its
              // declarations are never defined twice.

// The Drogon DbClient type we hold; it is the async gateway to PostgreSQL.
#include <drogon/orm/DbClient.h>

// std::shared_ptr, which lets several owners share one client safely.
#include <memory>
// std::string, the return type of the connection-string builder.
#include <string>

// Configurator::DatabaseConfig, the DB_* settings used to build the connection
// string.
#include "configurator/configurator.hpp"

/**
 * @class AsyncDbClient
 * @brief Store-registered holder for the Drogon PostgreSQL client used when
 * DB_ASYNC=true.
 * @details The wrapper holds a `shared_ptr<drogon::orm::DbClient>`; production
 * instances are created from the DB_* settings. `Server::run()` registers it in
 * the Store only while `DatabaseConfig::async` is enabled; the synchronous pool
 *          stays in use while the flag is off. Callers fetch it with
 * `Store::getObject<AsyncDbClient>(nullptr)` and hand the underlying client to
 * `asyncdb::execSqlCoro`. Tests replace the registered instance through
 *          `Store::replaceObject`; a null client is a valid state meaning
 * "async path unavailable", which the controller treats as `std::nullopt`. The
 * wrapper itself is non-copyable and non-movable so there is a single
 * long-lived Store-owned instance. It adds no locking of its own; any
 * thread-safety comes from the underlying Drogon DbClient.
 * @see createClient, asyncdb::execSqlCoro, DatabaseController::executeQueryCoro
 */
class AsyncDbClient
{
 public:
  /**
   * @brief Constructs the wrapper around an already-created Drogon client.
   * @param[in] client Client to share ownership of; may be null (used by
   * tests and the unavailable state).
   * @post `client()` returns the same pointer that was passed in.
   */
  explicit AsyncDbClient(
      std::shared_ptr<drogon::orm::DbClient>
          client);  // `explicit` prevents accidental implicit conversion from
                    // a bare pointer.

  /**
   * @brief Copy and move operations are disabled.
   * @details The wrapper is a single Store-owned object; tests swap the whole
   * `shared_ptr` via `Store::replaceObject` rather than copying or moving the
   * wrapper.
   */
  AsyncDbClient(const AsyncDbClient&) =
      delete;  // No second wrapper may be made by copying: that would blur
               // "one registered client".
  AsyncDbClient(AsyncDbClient&&) =
      delete;  // No move construction either: the Store
               // owns the object at a fixed address.
  AsyncDbClient& operator=(const AsyncDbClient&) =
      delete;  // No copy assignment: replacing the client is done through the
               // Store, not here.
  AsyncDbClient& operator=(AsyncDbClient&&) =
      delete;  // No move assignment: keeps the wrapper's
               // identity stable for its whole lifetime.

  /** @brief Defaulted virtual destructor; connection cleanup is delegated to
   * the owned Drogon client.
   *  @note Virtual per the project-wide convention (commit 217a477); no class
   * derives from this type.
   */
  virtual ~AsyncDbClient() =
      default;  // The shared_ptr member frees the client (and its pool) when
                // the last owner disappears.

  /**
   * @brief Builds a Drogon PostgreSQL client from the configuration held in
   * the Store.
   * @details Reads `Configurator::DatabaseConfig` from the Store, builds the
   * connection string with `connectionString`, requests `DB_MAX_CONN`
   * connections and applies the acquire timeout, converting milliseconds to
   * the seconds Drogon expects.
   * @return A newly created client, ready to be wrapped and registered with
   * `Store::registerObject<AsyncDbClient>`.
   * @pre A `Configurator` object is registered in the Store.
   */
  static std::shared_ptr<drogon::orm::DbClient>
  createClient();  // Factory: `static` means you call it on the class,
                   // without an existing object.

  /**
   * @brief Renders a libpq keyword/value connection string from a database
   * configuration.
   * @param[in] config Source of host, port, database name, user, password and
   * SSL settings.
   * @return `host=... port=... dbname=... user=... password=...
   * connect_timeout=2 sslmode=require|disable`.
   * @warning The returned string contains the database password in clear
   * text; it must not be logged. `printValues()` on the configuration
   * deliberately redacts the same field.
   */
  static std::string connectionString(
      const Configurator::DatabaseConfig&
          config);  // Pure formatting: no connection is opened, only text is
                    // produced.

  /**
   * @brief Returns the owned client, which may be null.
   * @return Const reference to the stored `shared_ptr`; it remains valid for
   * the lifetime of this wrapper.
   */
  [[nodiscard]] const std::shared_ptr<drogon::orm::DbClient>& client() const
  {
    return client_;
  }  // `[[nodiscard]]` warns if a caller ignores the answer; the reference
     // avoids copying the pointer.

 private:
  /**
   * Shared ownership of the Drogon client; null in test fakes and the
   * disabled state.
   */
  std::shared_ptr<drogon::orm::DbClient>
      client_;  // The single piece of state this class exists to hold;
                // `shared_ptr` keeps the pool alive as long as anyone uses
                // it.
};
