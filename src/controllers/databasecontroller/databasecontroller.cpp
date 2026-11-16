/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file databasecontroller.cpp
 * @brief Blocking (pqxx pool) and coroutine (AsyncDbClient/Drogon)
 * implementations of DatabaseController.
 * @details Synchronous methods funnel through the executer template; coroutine
 * methods each obtain the async client, short-circuit when it is absent, and
 * translate Drogon busy errors into `DatabaseBusy` for 503 responses.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the lowest layer that actually touches PostgreSQL. All the shared
 * machinery lives here: lazy, thread-safe creation of the connection pool, the
 * single `executer` template that acquires a connection and runs blocking SQL,
 * and the async family that runs on Drogon's IO loop. By converting "busy" into
 * a dedicated exception, this file lets the HTTP layer answer 503 with
 * Retry-After instead of treating overload as a broken query.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (resources released when objects go out of scope):
 * https://en.cppreference.com/w/cpp/language/raii
 * - Double-checked locking / std::atomic / std::scoped_lock:
 * https://en.cppreference.com/w/cpp/mutex/scoped_lock
 * - std::optional and std::nullopt:
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Templates (the `executer` helper):
 * https://en.cppreference.com/w/cpp/language/templates
 * - Coroutines (co_await / co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Exceptions and catch blocks:
 * https://en.cppreference.com/w/cpp/language/exceptions
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */
#include "databasecontroller.hpp"  // The class declaration being defined.

#include <fmt/core.h>    // Formatting library for error messages.
#include <fmt/format.h>  // Formatting with placeholders.

#include <cstdint>  // Fixed-width integer types.
#include <cstdlib>  // EXIT_FAILURE, referenced in the lazy-init comment.
#include <jsoncons/basic_json.hpp>  // JSON value type.
#include <mutex>          // std::scoped_lock used during one-time init.
#include <optional>       // std::nullopt for unavailable results.
#include <pqxx/pqxx>      // libpqxx transaction and params types.
#include <string>         // SQL text and identifiers.
#include <string_view>    // String views where needed.
#include <unordered_set>  // Schema/table result sets.

#include "configurator/configurator.hpp"  // Test configuration.
#include "database/database.hpp"          // The blocking SQL helper functions.
#include "database/databaseconnectionpool.hpp"  // The blocking connection pool.
#include "database/watchdog.hpp"                // Pool watchdog type.
#include "store/store.hpp"                      // Service locator for backends.
#include "utils/global/types.hpp"               // ColumnInfo and shared types.

DatabaseController::DatabaseController()
{
}  // Deliberately empty: backends are created lazily on first query.

void DatabaseController::ensureBackends()  // Thread-safe one-time creation of
                                           // pool + watchdog.
{
  // Lazily create the connection pool and watchdog on first use instead of
  // in the constructor: the pool constructor calls exit(EXIT_FAILURE) when
  // PostgreSQL is unreachable, which made the whole class unconstructible in
  // hermetic unit tests. The server still connects at startup, because
  // Server's constructor eagerly creates DatabaseSchema, which constructs
  // the (singleton) DatabaseController through Store and then performs the
  // first query (schema population) during boot.
  //
  // Every query calls this, so the common case (already created) is one
  // atomic load. The mutex is only taken during the first calls; the
  // release/acquire pair makes the pointers written below visible to every
  // thread that sees the flag set.
  if (backendsReady_.load(
          std::memory_order_acquire))  // Fast path: already initialized, so
                                       // return immediately.
  {
    return;
  }

  static std::mutex backendInitMutex;  // Function-local mutex guards the slow
                                       // first-init path.
  std::scoped_lock  lock(backendInitMutex);  // RAII lock: held only in this
                                             // scope, released automatically.
  if (backendsReady_.load(
          std::memory_order_relaxed))  // Re-check after locking: another
                                       // thread may have initialized.
  {
    return;
  }
  if (!databaseConnectionPool_)  // Only create backends if they do not exist
                                 // yet.
  {
    // Hermetic unit-test seam: when VALHALLA_TEST_NO_DB is set (the unit
    // test binary sets it before any Configurator is created), the pool
    // and watchdog are never created. Every query path then reports
    // "database unavailable" (std::nullopt) deterministically, with no
    // sockets opened and no retries. Production never sets the variable,
    // so behaviour is unchanged there.
    if (!testing_config_
             ->no_db)  // Skip creation entirely in hermetic test mode.
    {
      databaseConnectionPool_ =
          Store::getObject<DatabaseConnectionPool>();  // Resolve the shared
                                                       // pool from the service
                                                       // locator.
      watchDog_ = Store::getObject<WatchDog>();  // Resolve the shared watchdog.
    }
  }
  backendsReady_.store(true,
                       std::memory_order_release);  // Publish the pointers to
                                                    // all threads.
}

std::optional<jsoncons::json> DatabaseController::executeQuery(
    const std::string& query, const pqxx::params& params)
{
  // Non-transactional execution: a single statement owns its connection until
  // the handler is destroyed.
  return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json,
                                                          pqxx::nontransaction>,
                                  query,
                                  params);  // Run via the shared blocking
                                            // helper.
}

std::optional<jsoncons::json> DatabaseController::executeReadQuery(
    const std::string& query, const pqxx::params& params)
{
  return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json,
                                                          pqxx::nontransaction>,
                                  query,
                                  params);  // Run via the shared blocking
                                            // helper.
}

std::optional<jsoncons::json::array> DatabaseController::executeSearchQuery(
    const std::string& query, const pqxx::params& params)
{
  return executer<
      jsoncons::json::array>(&Database::executeQuery<jsoncons::json::array,
                                                     pqxx::nontransaction>,
                             query,
                             params);  // Map rows to a JSON array.
}

std::optional<std::string> DatabaseController::doReadQuery(
    const std::string& query, const pqxx::params& params)
{
  return executer<std::string>(&Database::doSimpleQuery<std::string>,
                               query,
                               params);  // Map the first value to a string.
}

std::optional<bool> DatabaseController::checkItemExists(
    const std::string& table,
    const std::string& column,
    const std::string& value)
{
  return executer<bool>(&Database::checkExists,
                        table,
                        column,
                        value);  // Delegate the EXISTS check.
}

std::optional<jsoncons::json> DatabaseController::getPasswordHashForUserName(
    const std::string& username, const std::string& tablename)
{
  // Identifiers cannot be bound: only the two client tables are accepted.
  if (tablename != "users" &&
      tablename !=
          "providers")  // Only fixed table names are safe to interpolate.
  {
    Message::ErrorMessage(
        fmt::format("Unknown client table: {}",
                    tablename));  // Log a rejected identifier.
    return std::nullopt;
  }
  const std::string query = fmt::
      format("SELECT id,password,active FROM {} WHERE username = $1 LIMIT 1;",
             tablename);  // Table name fixed; username bound as $1.
  return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json,
                                                          pqxx::nontransaction>,
                                  query,
                                  pqxx::params{
                                      username});  // Bind the username and run.
}

std::optional<std::unordered_set<api::v2::ColumnInfo>>
DatabaseController::getTableSchema(const std::string& tableName)
{
  return executer<std::unordered_set<
      api::v2::ColumnInfo>>(&Database::getTableSchema,
                            tableName);  // Delegate the schema load.
}

std::optional<std::unordered_set<std::string>>
DatabaseController::getAllTables()
{
  return executer<std::unordered_set<std::string>>(&Database::getAllTables);
}  // One-line delegation to the shared helper.

std::optional<jsoncons::json> DatabaseController::getPermissions(
    const std::string& query)
{
  return executer<jsoncons::json>(&Database::executeQuery<jsoncons::json,
                                                          pqxx::nontransaction>,
                                  query,
                                  pqxx::params{});
}

// Async client used by the coroutine methods.
#include "database/asyncdbclient.hpp"
#include "database/asyncdberror.hpp"       // isBusy() detects overload errors.
#include "database/asyncresultmapper.hpp"  // Maps async rows to JSON.
#include "database/asyncsqlbinder.hpp"     // Binds parameters for async SQL.

drogon::Task<std::optional<jsoncons::json>>
DatabaseController::executeQueryCoro(const std::string&  query,
                                     const pqxx::params& params)
{
  // All coroutine entry points share this guard: no async client means the DB
  // is unavailable, so report nullopt rather than throwing. The client is
  // obtained from the Store, not held as controller state.
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    // Drogon transaction: BEGIN is sent on creation, COMMIT on destruction,
    // ROLLBACK on exception - the asynchronous counterpart of the
    // pqxx::work used by the synchronous path.
    const auto transaction =
        co_await asyncClient->client()
            ->newTransactionCoro();  // BEGIN; COMMIT on scope exit,
                                     // ROLLBACK on error.
    const auto result =
        co_await asyncdb::execSqlCoro(*transaction,
                                      query,
                                      params);  // Execute SQL inside the
                                                // transaction.
    co_return asyncdb::mapResultToObject(
        result);  // Convert the rows into a JSON object.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::format("Exception occurred during async query execution: {}",
                    e.what()));
    // A busy database is not a failed query: propagate DatabaseBusy so the
    // HTTP layer can answer 503.
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<jsoncons::json>>
DatabaseController::executeReadQueryCoro(const std::string&  query,
                                         const pqxx::params& params)
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const auto result =
        co_await asyncdb::execSqlCoro(*asyncClient->client(),
                                      query,
                                      params);  // Run SQL without an explicit
                                                // transaction.
    co_return asyncdb::mapResultToObject(
        result);  // Convert the rows into a JSON object.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::format("Exception occurred during async read query execution: {}",
                    e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<jsoncons::json::array>>
DatabaseController::executeSearchQueryCoro(const std::string&  query,
                                           const pqxx::params& params)
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const auto result =
        co_await asyncdb::execSqlCoro(*asyncClient->client(),
                                      query,
                                      params);  // Run SQL without an explicit
                                                // transaction.
    co_return asyncdb::mapResultToArray(
        result);  // Convert the rows into a JSON array.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::
            format("Exception occurred during async search query execution: {}",
                   e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<std::string>> DatabaseController::doReadQueryCoro(
    const std::string& query, const pqxx::params& params)
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const auto result =
        co_await asyncdb::execSqlCoro(*asyncClient->client(),
                                      query,
                                      params);  // Run SQL without an explicit
                                                // transaction.
    // A simple read has no value to return when the result set is empty.
    if (result.empty())  // No row means there is no value to return.
    {
      co_return std::nullopt;  // Return an empty result; callers treat
                               // this as unavailable or failed.
    }
    co_return std::string(
        result[0][0].c_str());  // Return the very first cell as a string.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::
            format("Exception occurred during async simple query execution: {}",
                   e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<bool>> DatabaseController::checkItemExistsCoro(
    const std::string& table,
    const std::string& column,
    const std::string& value)
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const std::string query =
        fmt::format("SELECT EXISTS (SELECT 1 FROM {} WHERE {} = $1);",
                    table,
                    column);  // Identifiers fixed, value bound as $1.
    const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(),
                                                      query,
                                                      pqxx::params{value});
    // EXISTS always yields one row; an empty result means something went
    // wrong, not "false".
    if (result.empty())  // No row means there is no value to return.
    {
      co_return std::nullopt;  // Return an empty result; callers treat
                               // this as unavailable or failed.
    }
    co_return result[0][0]
        .as<bool>();  // EXISTS always yields one row; read its boolean.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::format("Exception occurred during async exists check: {}",
                    e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<jsoncons::json>>
DatabaseController::getPasswordHashForUserNameCoro(const std::string& username,
                                                   const std::string& tablename)
{
  if (tablename != "users" &&
      tablename !=
          "providers")  // Only fixed table names are safe to interpolate.
  {
    Message::ErrorMessage(
        fmt::format("Unknown client table: {}",
                    tablename));  // Log a rejected identifier.
    co_return std::nullopt;       // Return an empty result; callers treat this
                                  // as unavailable or failed.
  }
  const std::string query = fmt::
      format("SELECT id,password,active FROM {} WHERE username = $1 LIMIT 1;",
             tablename);  // Table name fixed; username bound as $1.
  co_return co_await executeReadQueryCoro(query,
                                          pqxx::params{
                                              username});  // Reuse the async
                                                           // read path with the
                                                           // bound username.
}

drogon::Task<std::optional<std::unordered_set<api::v2::ColumnInfo>>>
DatabaseController::getTableSchemaCoro(const std::string& tableName)
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const std::string query =
        "SELECT column_name, data_type, column_default, is_nullable FROM "
        "information_schema.columns WHERE table_name = $1 AND column_name "
        "!= 'id';";  // Metadata
    // query;
    // the
    // id
    // column
    // is
    // skipped.
    const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(),
                                                      query,
                                                      pqxx::params{tableName});

    std::unordered_set<api::v2::ColumnInfo>
        schema;  // Collected column descriptions.
    // Map each information_schema row; a NULL default becomes "None" and
    // isNullable follows the "YES" flag.
    for (drogon::orm::Result::SizeType row = 0; row < result.size();
         ++row)  // One iteration per returned column.
    {
      api::v2::ColumnInfo column;  // One column's metadata record.
      column.Name = std::string(
          result[row][0].c_str());  // Column name from the first field.
      column.DataType = std::string(result[row][1].c_str());
      column.Constraint =
          result[row][2].isNull()
              ? "None"
              : std::string(
                    result[row][2].c_str());  // A NULL default becomes 'None'.
      column.isNullable = std::string(result[row][3].c_str()) ==
                          "YES";  // 'YES' means the column may be NULL.
      schema.insert(column);  // Store the record in the set for the validator.
    }
    co_return schema;  // Hand the column set back to the caller.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::format("Exception occurred during async schema load: {}",
                    e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<std::unordered_set<std::string>>>
DatabaseController::getAllTablesCoro()
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const auto result = co_await asyncdb::
        execSqlCoro(*asyncClient->client(),
                    "SELECT table_name FROM information_schema.tables WHERE "
                    "table_schema = 'public';",
                    pqxx::params{});  // List public tables.

    std::unordered_set<std::string> tables;  // Collected table names.
    for (drogon::orm::Result::SizeType row = 0; row < result.size();
         ++row)  // One iteration per returned column.
    {
      tables.insert(std::string(
          result[row][0].c_str()));  // Add each table name to the set.
    }
    co_return tables;  // Hand the table set back to the caller.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::format("Exception occurred during async table list: {}",
                    e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}

drogon::Task<std::optional<jsoncons::json>>
DatabaseController::getPermissionsCoro(const std::string& query)
{
  auto asyncClient = Store::getObject<AsyncDbClient>(
      nullptr);  // Look up the async client; nullptr if not configured.
  if (asyncClient == nullptr ||
      asyncClient->client() ==
          nullptr)  // No client means the DB is unavailable.
  {
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
  try
  {
    const auto result = co_await asyncdb::execSqlCoro(*asyncClient->client(),
                                                      query,
                                                      pqxx::params{});
    co_return asyncdb::mapResultToObject(
        result);  // Convert the rows into a JSON object.
  }
  catch (const std::exception& e)
  {
    Message::CriticalMessage(
        fmt::format("Exception occurred during async permissions query: {}",
                    e.what()));
    if (asyncdb::isBusy(e))  // Distinct from a failed query: signals overload.
    {
      throw DatabaseBusy();  // Propagate so the HTTP layer can answer
                             // 503.
    }
    co_return std::nullopt;  // Return an empty result; callers treat this
                             // as unavailable or failed.
  }
}
