/**
 * @file databasecontroller.hpp
 * @brief Single gateway through which controllers execute SQL, in both blocking
 * and coroutine form.
 * @details Declares `DatabaseController`, the one class that knows how to run a
 * SQL statement and map the result to JSON. It offers a blocking family
 * (executeQuery, executeReadQuery, executeSearchQuery, ...) backed by a
 *          connection pool, and an async family (executeQueryCoro, ...) backed
 * by Drogon's AsyncDbClient. Every method is virtual so tests can swap in a
 * fake, and every one returns an `std::optional` that is empty when the
 * database is unavailable or the query fails.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the single choke point between the application and PostgreSQL. The
 * Controller class calls these methods, so connection handling, transaction
 * rules and error mapping (including the special DatabaseBusy signal) live in
 * one place. Without it, every caller would have to manage connections itself,
 * and "database down" could not be handled consistently as a 503 instead of a
 * crash.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions / polymorphism (all methods are virtual for test
 * injection): https://en.cppreference.com/w/cpp/language/virtual
 * - Connection pooling and lazy initialization:
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::optional (empty means "no result"):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::atomic (thread-safe flag):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - std::shared_ptr / std::unique_ptr (ownership of backends and connections):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

#include <fmt/core.h>    // Formatting library for error messages.
#include <fmt/format.h>  // Formatting with placeholders.

// std::atomic<bool> for the thread-safe 'backends ready' flag.
#include <atomic>
#include <cstdint>                  // Fixed-width integer types.
#include <exception>                // std::exception used in the catch blocks.
#include <jsoncons/basic_json.hpp>  // JSON value type used for results.
#include <jsoncons/json.hpp>        // JSON helpers and array type.
#include <memory>     // std::shared_ptr / std::unique_ptr for backends.
#include <mutex>      // std::mutex / scoped_lock guard one-time initialization.
#include <optional>   // The optional result of every query.
#include <pqxx/pqxx>  // libpqxx: the synchronous PostgreSQL client.
#include <string>     // SQL text and identifiers.
#include <unordered_set>  // Schema/table results are returned as sets.
#include <utility>        // std::forward for the helper template.

// Test configuration (e.g. no-db mode).
#include "configurator/configurator.hpp"
#include "database/asyncdbclient.hpp"      // Async DB client declaration.
#include "database/asyncresultmapper.hpp"  // Maps async rows to JSON.
#include "database/asyncsqlbinder.hpp"     // Binds parameters to async SQL.
#include "database/databasebusy.hpp"  // The exception meaning "pool exhausted".
#include "database/databasehandler.hpp"  // Hands out blocking connections.
// Decides whether a query may run on the current thread.
#include "database/dbprobe.hpp"
// Drogon Task return type of the async methods.
#include "drogon/utils/coroutine.h"
#include "store/store.hpp"  // Service locator for backends and fake injection.
#include "utils/global/types.hpp"     // ColumnInfo and other shared types.
#include "utils/message/message.hpp"  // Logging helpers.
class Case;     // Forward declaration: only a pointer/name is needed in this
                // header.
class Service;  // Forward declaration to avoid a heavy include.
class Appointment;  // Forward declaration to avoid a heavy include.
class Database;     // Forward declaration of the blocking DB helper.
class WatchDog;     // Forward declaration of the pool watchdog.
/**
 * @class DatabaseController
 * @brief Single gateway through which controllers execute SQL, in both blocking
 * and coroutine form.
 * @details Owns no query logic of its own: it acquires a connection
 * (synchronously through a `DatabaseConnectionPool` and `DatabaseHanndler`, or
 * asynchronously through `AsyncDbClient`), invokes the matching `Database`/
 *          `asyncdb` helper, and returns an `std::optional` result that is
 * empty when the database is unavailable, a connection cannot be acquired, or
 * the query fails. Every public method is virtual so tests can inject fakes
 * through `Store::replaceObject`.
 *
 *          Backend construction is lazy (see ensureBackends): the pool
 * constructor terminates the process when PostgreSQL is unreachable, which
 * would otherwise make the class unconstructible in hermetic tests. The
 *          double-checked `backendsReady_` flag makes that init a single atomic
 * load on the hot path. In hermetic test mode the pool is never created and all
 * query paths deterministically return `std::nullopt`.
 *
 *          Query results are returned as JSON objects/arrays or plain scalars.
 * Identifiers (table/column names) cannot be bound as parameters, so the few
 * methods that interpolate them accept only fixed, trusted names.
 * @note Non-copyable and non-movable: the singleton is shared through `Store`,
 * never duplicated.
 * @warning Pool exhaustion is not a query failure: `DatabaseBusy` is propagated
 * so callers can answer 503 with Retry-After instead of 500.
 * @see Controller, Store, DatabaseBusy
 */
class DatabaseController {
 public:
  /**
   * @brief Constructs the controller without creating database backends.
   * @details Backends are created on the first query by ensureBackends() so
   * construction stays safe in tests.
   */
  DatabaseController();  // Does not create backends; that is deferred to the
                         // first query.
  DatabaseController(const DatabaseController&) =
      delete;  // Singleton: copying would duplicate backends.
  DatabaseController(DatabaseController&&) =
      delete;  // Moving is disabled for the same reason.
  DatabaseController& operator=(const DatabaseController&) =
      delete;  // No copy assignment on a singleton.
  DatabaseController& operator=(DatabaseController&&) =
      delete;  // No move assignment on a singleton.
  virtual ~DatabaseController() =
      default;  // Virtual for fake injection; default cleanup.

  /**
   * @brief Executes a write/statement query and maps one JSON object result.
   * @param[in] query Parameterized SQL statement.
   * @param[in] params Bound parameters for the statement's placeholders.
   * @return The JSON object, or `std::nullopt` if the DB is unavailable or
   * the query fails.
   * @throws DatabaseBusy if no connection became available within the
   * configured timeout.
   */
  virtual std::optional<jsoncons::json> executeQuery(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Blocking write/statement returning one JSON object.
  /**
   * @brief Executes a read query and maps one JSON object result.
   * @details Currently delegates to the same `Database::executeQuery`
   * instantiation as executeQuery(); the separate entry point keeps the
   * read/write intent explicit at call sites and as a separate override hook.
   * @param[in] query Parameterized SQL SELECT.
   * @param[in] params Bound parameters.
   * @return The JSON object, or `std::nullopt` on unavailable DB/query
   * failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<jsoncons::json> executeReadQuery(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Blocking SELECT returning one JSON object.
  /**
   * @brief Executes a search query and maps the result rows to a JSON array.
   * @param[in] query Parameterized SQL SELECT (typically the search builder's
   * `limit + 1` probe query).
   * @param[in] params Bound parameters.
   * @return The JSON array, or `std::nullopt` on unavailable DB/query
   * failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<jsoncons::json::array> executeSearchQuery(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Blocking SELECT returning a JSON array of rows.
  /**
   * @brief Executes a simple query and returns its first value as a raw
   * string.
   * @param[in] query Parameterized SQL statement.
   * @param[in] params Bound parameters.
   * @return The string result, or `std::nullopt` on unavailable DB/query
   * failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<std::string> doReadQuery(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Blocking query returning a single raw string.
  /**
   * @brief Checks existence with `Database::checkExists`.
   * @param[in] table Trusted table identifier (interpolated, not bound).
   * @param[in] column Trusted column identifier (interpolated, not bound).
   * @param[in] value Value bound as a `$1` parameter.
   * @return Whether a matching row exists, or `std::nullopt` on unavailable
   * DB/query failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<bool> checkItemExists(
      const std::string& table,
      const std::string& column,
      const std::string&
          value);  // Blocking EXISTS check; value is bound, names trusted.
  /**
   * @brief Fetches the id, password hash and active flag for a username in a
   * client table.
   * @details Only `users` and `providers` are accepted because identifiers
   * cannot be bound; any other table name is logged as an error and yields
   * `std::nullopt` without issuing a query.
   * @param[in] username Username to look up (bound as `$1`).
   * @param[in] tablename Either "users" or "providers".
   * @return JSON object containing id/password/active, or `std::nullopt` on
   * error/unknown table.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<jsoncons::json> getPasswordHashForUserName(
      const std::string& username,
      const std::string& tablename);  // Fetches id/password/active for login.
  /**
   * @brief Loads the column metadata of a table (excluding the `id` column in
   * the underlying query).
   * @param[in] tableName Table to describe (bound as a parameter).
   * @return Set of ColumnInfo records, or `std::nullopt` on unavailable
   * DB/query failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(
      const std::string&
          tableName);  // Loads a table's column metadata for validation.
  /**
   * @brief Lists all tables in the `public` schema.
   * @return Set of table names, or `std::nullopt` on unavailable DB/query
   * failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<std::unordered_set<std::string>>
  getAllTables();  // Lists all public tables (used at startup).
  /**
   * @brief Runs a caller-supplied permissions query and maps the result to a
   * JSON object.
   * @param[in] query Parameterized SQL supplied by the authorization layer.
   * @return The JSON object, or `std::nullopt` on unavailable DB/query
   * failure.
   * @throws DatabaseBusy if the pool is exhausted.
   */
  virtual std::optional<jsoncons::json> getPermissions(
      const std::string&
          query);  // Runs an authorization query from GateKeeper.

  // Asynchronous (coroutine) entry points used when DB_ASYNC=true. They run
  // on the Drogon IO loop without a worker hand-off.
  /**
   * @brief Async counterpart of executeQuery(); begins a transaction,
   * executes and commits on success.
   * @details A null/missing `AsyncDbClient` yields `std::nullopt`. On a busy
   * error the coroutine throws `DatabaseBusy` after logging; other exceptions
   * are logged and become `std::nullopt`.
   * @param[in] query Parameterized SQL statement.
   * @param[in] params Bound parameters.
   * @return A task resolving to the JSON object, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<jsoncons::json>> executeQueryCoro(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Async write with a transaction; co_await it.
  /**
   * @brief Async counterpart of executeReadQuery(); executes without an
   * explicit transaction.
   * @param[in] query Parameterized SQL SELECT.
   * @param[in] params Bound parameters.
   * @return A task resolving to the JSON object, or `std::nullopt` if
   * unavailable/busy/failed.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<jsoncons::json>> executeReadQueryCoro(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Async read without an explicit transaction.
  /**
   * @brief Async counterpart of executeSearchQuery(); maps rows to a JSON
   * array.
   * @param[in] query Parameterized SQL SELECT.
   * @param[in] params Bound parameters.
   * @return A task resolving to the JSON array, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<jsoncons::json::array>>
  executeSearchQueryCoro(
      const std::string&  query,
      const pqxx::params& params = {});  // Async search returning a JSON array.
  /**
   * @brief Async counterpart of doReadQuery(); returns the first cell as a
   * string.
   * @param[in] query Parameterized SQL statement.
   * @param[in] params Bound parameters.
   * @return A task resolving to the string, or `std::nullopt` when no
   * rows/unavailable/busy/failed.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<std::string>> doReadQueryCoro(
      const std::string&  query,
      const pqxx::params& params =
          {});  // Async query returning the first cell as a string.
  /**
   * @brief Async counterpart of checkItemExists().
   * @details Builds `SELECT EXISTS (SELECT 1 FROM <table> WHERE <column> =
   * $1)`; table and column are interpolated identifiers and must be trusted,
   * while @p value is bound.
   * @param[in] table Trusted table identifier.
   * @param[in] column Trusted column identifier.
   * @param[in] value Value bound as `$1`.
   * @return A task resolving to the boolean result, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<bool>> checkItemExistsCoro(
      const std::string& table,
      const std::string& column,
      const std::string& value);  // Async EXISTS check.
  /**
   * @brief Async counterpart of getPasswordHashForUserName(); same
   * fixed-table restriction applies.
   * @param[in] username Username to look up (bound as `$1`).
   * @param[in] tablename Either "users" or "providers".
   * @return A task resolving to id/password/active JSON, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<jsoncons::json>>
  getPasswordHashForUserNameCoro(
      const std::string& username,
      const std::string& tablename);  // Async login credential lookup.
  /**
   * @brief Async counterpart of getTableSchema(); maps information_schema
   * rows to ColumnInfo records.
   * @param[in] tableName Table to describe (bound as a parameter).
   * @return A task resolving to the column set, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<std::unordered_set<api::v2::ColumnInfo>>>
  getTableSchemaCoro(
      const std::string& tableName);  // Async column metadata load.
  /**
   * @brief Async counterpart of getAllTables(); lists tables in the `public`
   * schema.
   * @return A task resolving to the table-name set, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<std::unordered_set<std::string>>>
  getAllTablesCoro();  // Async public table list.
  /**
   * @brief Async counterpart of getPermissions(); maps a permissions query to
   * a JSON object.
   * @param[in] query Parameterized SQL supplied by the authorization layer.
   * @return A task resolving to the JSON object, or `std::nullopt`.
   * @throws DatabaseBusy when the database reports busy.
   */
  virtual drogon::Task<std::optional<jsoncons::json>> getPermissionsCoro(
      const std::string& query);  // Async permissions query.

 private:
  /**
   * Set once the pool/watchdog have been created (or deliberately skipped in
   * test mode); published with release/acquire.
   */
  std::atomic<bool> backendsReady_{
      false};  // Starts false; flips true after one-time backend setup.
  /**
   * Lazily created blocking connection pool; null in hermetic test mode.
   */
  std::shared_ptr<DatabaseConnectionPool>
      databaseConnectionPool_;  // Lazily created blocking pool; null in test
                                // mode.
  /**
   * Lazily created watchdog for the blocking pool; unused in hermetic test
   * mode.
   */
  std::shared_ptr<WatchDog>
      watchDog_;  // Lazily created watchdog for the blocking pool.
  /**
   * Test configuration read at first use to decide whether backends should be
   * skipped.
   */
  std::shared_ptr<Configurator::Testing> testing_config_ =
      std::make_shared<Configurator::Testing>();  // Read at first use to
                                                  // decide whether to skip
                                                  // backends.
  /**
   * Lazily creates the connection pool and watchdog
   */
  void
  ensureBackends();  // Creates the pool/watchdog once, safely across threads.

  /**
   * @brief Common implementation for every synchronous query method.
   * @details `DbProbe::check()` may throw `WouldBlock` to move a request off
   * the IO thread before this runs. After ensuring backends exist, a missing
   * pool (test mode) or missing connection yields `nullopt`. The supplied
   * function is invoked with the acquired `Database*` and forwarded
   * arguments.
   * @tparam Result Type wrapped in the returned optional (JSON object/array,
   * string, bool, or a set).
   * @tparam Func Callable taking a `Database*` followed by @p args.
   * @tparam Args Forwarded argument types.
   * @param[in] func Database helper to invoke.
   * @param[in] args Arguments forwarded to @p func (query text and
   * parameters).
   * @return The helper's result, or `std::nullopt` on unavailable DB/no
   * connection/query failure.
   * @throws DatabaseBusy rethrown so callers can answer 503; other exceptions
   * are logged and swallowed.
   */
  template <typename Result, typename Func, typename... Args>
  std::optional<Result> executer(const Func& func, Args&&... args) {
    DbProbe::check();  // on the IO-thread fast path this throws WouldBlock:
                       // the request moves to the DB executor
    try {
      ensureBackends();

      // Hermetic test mode: no backends were created, so report the
      // database as unavailable without constructing a handler (which
      // would create a pool via Store and exit() on init failure).
      if (!databaseConnectionPool_) {
        return std::nullopt;
      }

      std::unique_ptr<DatabaseHanndler> dbHandler =
          std::make_unique<DatabaseHanndler>(databaseConnectionPool_);
      std::shared_ptr<Database> db_ptr = dbHandler->get_connection();

      if (db_ptr == nullptr) {
        return std::nullopt;
      }

      std::optional<Result> results =
          std::invoke(func, db_ptr.get(), std::forward<Args>(args)...);

      if (results.has_value()) {
        return results;
      }

      return std::nullopt;
    } catch (const DatabaseBusy&) {
      throw;  // the pool is exhausted: callers answer 503 instead of
              // treating it as a failed query
    } catch (const std::exception& e) {
      Message::CriticalMessage(
          fmt::format("Exception occurred during query execution: {}",
                      e.what()));
      return std::nullopt;
    } catch (...) {
      Message::CriticalMessage(
          "Unknown exception occurred during query execution.");
      return std::nullopt;
    }
  }
};
