/**
 * @file controller.hpp
 * @brief Reusable persistence engine for the API's CRUDS
 * (Create/Read/Update/Delete/Search) operations.
 * @details This header defines the `Controller` class, a template-heavy helper
 * that turns an entity object into SQL and turns the database's answer into an
 * HTTP status plus a response body. It offers both ordinary blocking methods
 * (Create, Read, ...) and coroutine versions (CreateAsync, ReadAsync, ...) for
 * the async server loop. It is meant to be read from top to bottom: first the
 * constructor, then the public CRUDS helpers, then the two shared
 * `cruds`/`crudsAsync` implementations near the bottom.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the storage engine that sits underneath the request-facing handlers.
 * When an HTTP request arrives, Drogon routes it to an EntityController /
 * ClientController / ClinicController, and those classes call this Controller
 * to do the actual work. This class in turn calls DatabaseController (see
 * databasecontroller.hpp) to run parameterized SQL against PostgreSQL. Without
 * this file the handlers would have no way to insert, read, update, delete or
 * search rows, and the callback that finally answers the client would never be
 * invoked.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Templates: https://en.cppreference.com/w/cpp/language/templates
 * - Pointer-to-member function (used to pick an entity's SQL builder at
 * runtime): https://en.cppreference.com/w/cpp/language/pointer
 * - std::optional (a value that may be missing):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::shared_ptr (shared ownership of the database service):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - Move semantics (std::move / std::forward):
 * https://en.cppreference.com/w/cpp/language/move_constructor
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Lambda expressions: https://en.cppreference.com/w/cpp/language/lambda
 * - virtual functions / polymorphism:
 * https://en.cppreference.com/w/cpp/language/virtual
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

#include <fmt/core.h>  // Include fmt library for string formatting
// Formatting library: replaces {} placeholders with values.
#include <fmt/format.h>
// Extends fmt so it can format whole containers (ranges).
#include <fmt/ranges.h>

#include <cstddef>  // Provides size_t, the type used for sizes and counts.
#include <cstdint>  // Provides fixed-width integers such as uint64_t.
// std::exception, caught to turn errors into HTTP replies.
#include <exception>
// The jsoncons JSON value type used for bodies.
#include <jsoncons/basic_json.hpp>
#include <memory>       // std::shared_ptr for sharing the database service.
#include <optional>     // std::optional: holds a value that may be absent.
#include <stdexcept>    // std::runtime_error, thrown when setup fails.
#include <string>       // std::string, used for SQL text and response bodies.
#include <type_traits>  // Compile-time checks such as is_base_of_v / is_same_v.
#include <utility>      // std::move / std::forward helpers.

// The SQL gateway this engine calls.
#include "controllers/databasecontroller/databasecontroller.hpp"
#include "database/asyncdbclient.hpp"  // Async DB client declaration.
// Drogon Task type used by the Async variants.
#include "drogon/utils/coroutine.h"
// Client entity, used by the suspend/search constraints.
#include "entities/base/client.hpp"
// Data carriers such as Search_t used by search.
#include "entities/base/types.hpp"
// Patient entity, the only type allowed to fetch visits.
#include "entities/services/clinics/patient/patient.hpp"
#include "gatekeeper/gatekeeper.hpp"  // Authorization service declaration.
// Service locator that supplies the shared DatabaseController.
#include "store/store.hpp"
// Defines CALLBACK_, the reply function type.
#include "utils/global/callback.hpp"
#include "utils/global/global.hpp"  // Project-wide shared helpers and macros.
// HTTP status constants (OK, BAD_REQUEST, ...).
#include "utils/global/http.hpp"
// JSON helper used to serialize results.
#include "utils/jsonhelper/jsonhelper.hpp"
// Logging macros such as CRITICALMESSAGERESPONSE.
#include "utils/message/message.hpp"

// Convenience aliases for the entity data carriers used by the CRUDS helpers
// below.
using Search_t =
    api::v2::Types::Search_t;    // Short name for the search criteria carrier
                                 // (limit/offset/filters).
using Client = api::v2::Client;  // Short name for the client entity base class.
using GateKeeper =
    api::v2::GateKeeper;  // Short name for the authorization service.

/**
 * @class Controller
 * @brief Reusable persistence engine for the API's CRUDS
 * (Create/Read/Update/Delete/Search) operations.
 * @details Sits below the request-facing controllers (EntityController and its
 * derived classes). It owns the single `DatabaseController` obtained from the
 * `Store` singleton, turns an entity's SQL-builder methods into parameterized
 * statements, executes them, and maps results or failures onto an HTTP
 * status/body pair delivered through a `CALLBACK_`. Every operation has a
 * synchronous variant (which calls the blocking `DatabaseController` methods)
 * and a `drogon::Task` coroutine variant (which awaits the `*Coro` methods on
 * the Drogon IO loop).
 *
 *          Entities are passed by non-const reference because their
 * `getSql*Statement(pqxx::params&)` methods fill the parameter bag used by the
 * query; the controller does not take ownership of them. The callback is moved
 * into the operation and is invoked exactly once on every path that returns
 * normally, including validation and database failures. `CALLBACK_` is
 * `std::function<void(int, const std::string&)>`, so the first argument is an
 * HTTP status code and the second a response body (usually serialized JSON).
 *
 *          This class is copyable/movable by design: copies share the same
 * `DatabaseController` through a `std::shared_ptr`. No mutable per-instance
 * state is touched after construction, so concurrent use of distinct instances
 * is independent; the thread-safety of the shared `DatabaseController` itself
 * is not specified here.
 * @see EntityController, DatabaseController
 */
class Controller
{
 public:
  /**
   * @brief Resolves the shared `DatabaseController` service from the `Store`
   * singleton.
   * @details All operations depend on that service; resolving it here lets
   * derived request-scoped controllers stay cheap to construct.
   * `Store::getObject` may create the singleton on first use.
   * @throws std::runtime_error if the database controller cannot be obtained
   * (a critical message is logged first).
   */
  Controller()
  {
    try
    {
      databaseController =
          Store::getObject<DatabaseController>();  // Ask the service locator
                                                   // for the single shared DB
                                                   // gateway.
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGE  // Log a critical, file/line-tagged error before
                       // giving up.
          throw std::runtime_error(
              // Without a DB gateway this object is useless, so stop
              // construction.
              "Failed to initialize database controller");
    }
  }

  /**
   * Copy/move operations are defaulted; the `DatabaseController` is shared,
   * not duplicated.
   */
  Controller& operator=(const Controller&) =
      default;  // Copying just copies the shared_ptr,
                // not the database connection.
  Controller(const Controller&) =
      default;  // Copies are cheap: the DB service is shared, not duplicated.
  Controller& operator=(Controller&&) =
      default;  // Move assignment transfers the shared handle.
  Controller(Controller&&) =
      default;  // Move construction transfers the shared handle.

  /**
   * @brief Virtual destructor so deletion through a base pointer is
   * well-defined.
   */
  virtual ~Controller() = default;  // Virtual so deleting via a base pointer
                                    // is safe; default body needs no cleanup.

  // CRUDS
  /**
   * @brief Inserts @p entity and reports the result through @p callback.
   * @tparam T Entity type exposing `getSqlCreateStatement(pqxx::params&)` and
   * `getTableName()`.
   * @param[in,out] entity Entity whose fields are bound into the INSERT
   * statement; not owned by the controller.
   * @param[in,out] callback Invoked once with 200/400 (or the mapped error
   * status) and a response body.
   * @note Uses the write executor (`DatabaseController::executeQuery`); the
   * database assigns the primary key.
   * @see cruds
   */
  template <typename T>
  void Create(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlCreateStatement;  // Pointer to the INSERT builder; may
                                    // return no statement if data is
                                    // invalid.
    cruds(entity,
          sqlstatement,
          dbexec,
          std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Coroutine counterpart of Create().
   * @details Awaits `DatabaseController::executeQueryCoro` on the Drogon IO
   * loop (no worker hand-off) before invoking @p callback. The entity is
   * borrowed: @p entity must outlive the coroutine.
   * @tparam T Entity type exposing `getSqlCreateStatement(pqxx::params&)`.
   * @param[in,out] entity Entity to insert; must remain valid until the
   * coroutine completes.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see crudsAsync, Create
   */
  template <typename T>
  drogon::Task<void> CreateAsync(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlCreateStatement;  // Pointer to the INSERT builder; may
                                    // return no statement if data is
                                    // invalid.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Reads @p entity's requested columns by id and reports the JSON row
   * through @p callback.
   * @tparam T Entity type exposing `getSqlReadStatement(pqxx::params&)`.
   * @param[in,out] entity Carries the target id and column selection; not
   * owned by the controller.
   * @param[in,out] callback Invoked once with 200/400 (or the mapped error
   * status) and a response body.
   * @note Uses the read executor (`DatabaseController::executeReadQuery`).
   * @see cruds
   */
  template <typename T>
  void Read(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlReadStatement;  // Pointer to the SELECT builder for
                                  // reading selected columns.
    cruds(entity,
          sqlstatement,
          dbrexec,
          std::forward<CALLBACK_>(std::move(callback)));
  }
  /**
   * @brief Coroutine counterpart of Read().
   * @details Awaits `DatabaseController::executeReadQueryCoro` on the Drogon
   * IO loop before invoking @p callback. The read path deliberately uses the
   * read-specific executor.
   * @tparam T Entity type exposing `getSqlReadStatement(pqxx::params&)`; must
   * outlive the coroutine.
   * @param[in,out] entity Borrowed entity carrying id/columns.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see crudsAsync, Read
   */
  template <typename T>
  drogon::Task<void> ReadAsync(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlReadStatement;  // Pointer to the SELECT builder for
                                  // reading selected columns.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeReadQueryCoro,
                        std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Updates @p entity's persisted row and reports the result through
   * @p callback.
   * @tparam T Entity type exposing `getSqlUpdateStatement(pqxx::params&)`.
   * @param[in,out] entity Holds the new values and the target id; not owned
   * by the controller.
   * @param[in,out] callback Invoked once with 200/400 (or the mapped error
   * status) and a response body.
   * @note Uses the write executor (`DatabaseController::executeQuery`).
   * @see cruds
   */
  template <typename T>
  void Update(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlUpdateStatement;  // Pointer to the UPDATE builder for the
                                    // target row.
    cruds(entity,
          sqlstatement,
          dbexec,
          std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Coroutine counterpart of Update().
   * @details Awaits `DatabaseController::executeQueryCoro` on the Drogon IO
   * loop before invoking @p callback.
   * @tparam T Entity type exposing `getSqlUpdateStatement(pqxx::params&)`;
   * must outlive the coroutine.
   * @param[in,out] entity Borrowed entity carrying the new values and id.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see crudsAsync, Update
   */
  template <typename T>
  drogon::Task<void> UpdateAsync(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlUpdateStatement;  // Pointer to the UPDATE builder for the
                                    // target row.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Deletes @p entity's persisted row and reports the result through
   * @p callback.
   * @tparam T Entity type exposing `getSqlDeleteStatement(pqxx::params&)`.
   * @param[in,out] entity Borrowed entity carrying the id to delete.
   * @param[in,out] callback Invoked once with 200/400 (or the mapped error
   * status) and a response body.
   * @note Uses the write executor (`DatabaseController::executeQuery`).
   * @see cruds
   */
  template <typename T>
  void Delete(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlDeleteStatement;  // Pointer to the DELETE builder for the
                                    // target id.
    cruds(entity,
          sqlstatement,
          dbexec,
          std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Coroutine counterpart of Delete().
   * @details Awaits `DatabaseController::executeQueryCoro` on the Drogon IO
   * loop before invoking @p callback.
   * @tparam T Entity type exposing `getSqlDeleteStatement(pqxx::params&)`;
   * must outlive the coroutine.
   * @param[in,out] entity Borrowed entity carrying the id to delete.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see crudsAsync, Delete
   */
  template <typename T>
  drogon::Task<void> DeleteAsync(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlDeleteStatement;  // Pointer to the DELETE builder for the
                                    // target id.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::forward<CALLBACK_>(std::move(callback)));
  }

  /**
   * @brief Runs @p entity's search statement and returns a paginated JSON
   * envelope through @p callback.
   * @details The response object always contains `more` (bool, initially
   * false), `offset` (initially 0) and `results` (an array; empty when no
   * query or no rows). The search query is built from the `Search_t` held by
   * the entity. When the returned row count exceeds the requested `limit`,
   * one extra probe row is fetched (the entity's search builder binds `limit
   * + 1`): it is removed, `more` is set to true and `offset` advances by
   * `limit`.
   * @tparam T Entity type exposing `getSqlSearchStatement(pqxx::params&)` and
   * `getData()` convertible to Search_t.
   * @param[in,out] entity Borrowed entity carrying limit/offset and the
   * search criteria.
   * @param[in,out] callback Invoked once with 200 and the serialized envelope
   * (or a mapped error status).
   * @note Exceptions (including `DatabaseBusy`) are turned into an HTTP
   * status by `CRITICALMESSAGERESPONSE`.
   * @see executeSearchQuery (DatabaseController)
   */
  template <typename T>
  void Search(T& entity, CALLBACK_&& callback)
  {
    jsoncons::json response_json;
    response_json["more"]   = false;  // Assume no further page until the
                                      // row-count probe proves otherwise.
    response_json["offset"] = 0;      // Start the envelope at offset zero; a
                                      // real offset is set when 'more' is true.

    std::optional<jsoncons::json>
        query_results_json_array;      // Holds the rows once the search
                                       // returns; empty means no query ran.
    std::optional<std::string> query;  // Holds the built SQL; empty means the
                                       // entity produced nothing to run.

    try
    {
      pqxx::params params;  // The bag of values bound to the SQL
                            // placeholders ($1, $2, ...).
      // The entity builds both the parameterized statement and its bound
      // values.
      query = entity.getSqlSearchStatement(
          params);  // Ask the entity to build the SQL and fill the
                    // parameter bag.

      if (query.has_value())
      {
        query_results_json_array =
            databaseController
                ->executeSearchQuery(query.value(),
                                     params);  // Blocking search that maps rows
                                               // to a JSON array.

        if (query_results_json_array.has_value() &&
            !query_results_json_array->empty())
        {
          size_t results_count =
              query_results_json_array->size();  // Count rows to detect the
                                                 // extra probe row (limit + 1).
          // More rows than requested means an over-fetch: drop the
          // extra row and advertise the next page.
          if (results_count > std::get<Search_t>(entity.getData()).limit)
          {
            response_json["more"] = true;  // An extra row exists, so tell the
                                           // client another page is available.
            response_json["offset"] =
                std::get<Search_t>(entity.getData()).offset +
                std::get<Search_t>(entity.getData())
                    .limit;  // Next page starts after this page's
                             // limit.
            query_results_json_array->erase(
                query_results_json_array->array_range().end() -
                1);  // Remove the probe row so the client sees only
                     // 'limit' rows.
          }
        }
      }
      // Absent results still yield a well-formed envelope with an empty
      // array.
      if (query_results_json_array.has_value())
      {
        response_json["results"] = std::move(query_results_json_array.value());
      }
      else
      {
        response_json["results"] =
            jsoncons::json::array();  // Guarantee an empty array
                                      // instead of a null when nothing
                                      // was found.
      }

      // Marshal the envelope and answer once.
      std::move(callback)(api::v2::Http::Status::OK,
                          response_json.as<std::string>());
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }
  /**
   * @brief Coroutine counterpart of Search().
   * @details Awaits `DatabaseController::executeSearchQueryCoro` on the
   * Drogon IO loop, then applies the same over-fetch/pagination and envelope
   * construction as Search() before invoking @p callback.
   * @tparam T Entity type exposing `getSqlSearchStatement(pqxx::params&)` and
   * `getData()` convertible to Search_t; must outlive the coroutine.
   * @param[in,out] entity Borrowed entity carrying limit/offset and search
   * criteria.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see Search, crudsAsync
   */
  template <typename T>
  drogon::Task<void> SearchAsync(T& entity, CALLBACK_&& callback)
  {
    jsoncons::json response_json;
    response_json["more"]   = false;  // Assume no further page until the
                                      // row-count probe proves otherwise.
    response_json["offset"] = 0;      // Start the envelope at offset zero; a
                                      // real offset is set when 'more' is true.

    std::optional<jsoncons::json>
        query_results_json_array;      // Holds the rows once the search
                                       // returns; empty means no query ran.
    std::optional<std::string> query;  // Holds the built SQL; empty means the
                                       // entity produced nothing to run.

    try
    {
      pqxx::params params;  // The bag of values bound to the SQL
                            // placeholders ($1, $2, ...).
      query = entity.getSqlSearchStatement(
          params);  // Ask the entity to build the SQL and fill the
                    // parameter bag.

      if (query.has_value())
      {
        query_results_json_array =
            co_await databaseController
                ->executeSearchQueryCoro(query.value(),
                                         params);  // Await the async search
                                                   // without blocking the IO
                                                   // loop.

        if (query_results_json_array.has_value() &&
            !query_results_json_array->empty())
        {
          size_t results_count =
              query_results_json_array->size();  // Count rows to detect the
                                                 // extra probe row (limit + 1).
          if (results_count > std::get<Search_t>(entity.getData()).limit)
          {
            response_json["more"] = true;  // An extra row exists, so tell the
                                           // client another page is available.
            response_json["offset"] =
                std::get<Search_t>(entity.getData()).offset +
                std::get<Search_t>(entity.getData())
                    .limit;  // Next page starts after this page's
                             // limit.
            query_results_json_array->erase(
                query_results_json_array->array_range().end() -
                1);  // Remove the probe row so the client sees only
                     // 'limit' rows.
          }
        }
      }
      if (query_results_json_array.has_value())
      {
        response_json["results"] = std::move(query_results_json_array.value());
      }
      else
      {
        response_json["results"] =
            jsoncons::json::array();  // Guarantee an empty array
                                      // instead of a null when nothing
                                      // was found.
      }

      std::move(callback)(api::v2::Http::Status::OK,
                          response_json.as<std::string>());
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
    co_return;
  }
  /**
   * @brief Deactivates a client account and reports the result through @p
   * callback.
   * @details Delegates to the generic write path using the entity's
   * `getSqlSuspendStatement`.
   * @tparam T Client-derived entity type (must derive from
   * `api::v2::Client`).
   * @param[in,out] entity Borrowed client entity carrying the id to suspend.
   * @param[in,out] callback Invoked once with 200/400 (or the mapped error
   * status) and a response body.
   * @see cruds, Unsuspend
   */
  template <typename T>
  void Suspend(T& entity, CALLBACK_&& callback)
    requires(std::is_base_of_v<Client, T>)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlSuspendStatement;  // Pointer to the builder that flips a
                                     // client to inactive.
    cruds(entity, sqlstatement, dbexec, std::move(callback));
  }

  /**
   * @brief Reactivates a previously suspended client account.
   * @details Delegates to the generic write path using
   * `getSqlActivateStatement`.
   * @tparam T Client-derived entity type.
   * @param[in,out] entity Borrowed client entity carrying the id to activate.
   * @param[in,out] callback Invoked once with 200/400 (or the mapped error
   * status) and a response body.
   * @see cruds, Suspend
   */
  template <typename T>
  void Unsuspend(T& entity, CALLBACK_&& callback)
    requires(std::is_base_of_v<Client, T>)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlActivateStatement;  // Pointer to the builder that flips a
                                      // client back to active.
    cruds(entity, sqlstatement, dbexec, std::move(callback));
  }

  /**
   * @brief Coroutine counterpart of Suspend().
   * @details Awaits `DatabaseController::executeQueryCoro` on the Drogon IO
   * loop.
   * @tparam T Client-derived entity type; must outlive the coroutine.
   * @param[in,out] entity Borrowed client entity carrying the id to suspend.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see crudsAsync, Suspend
   */
  template <typename T>
  drogon::Task<void> SuspendAsync(T& entity, CALLBACK_&& callback)
    requires(std::is_base_of_v<Client, T>)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlSuspendStatement;  // Pointer to the builder that flips a
                                     // client to inactive.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::move(callback));
  }

  /**
   * @brief Coroutine counterpart of Unsuspend().
   * @details Awaits `DatabaseController::executeQueryCoro` on the Drogon IO
   * loop.
   * @tparam T Client-derived entity type; must outlive the coroutine.
   * @param[in,out] entity Borrowed client entity carrying the id to activate.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see crudsAsync, Unsuspend
   */
  template <typename T>
  drogon::Task<void> UnsuspendAsync(T& entity, CALLBACK_&& callback)
    requires(std::is_base_of_v<Client, T>)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlActivateStatement;  // Pointer to the builder that flips a
                                      // client back to active.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::move(callback));
  }

  /**
   * @brief Retrieves the services associated with a client and returns them
   * as a JSON array.
   * @details Builds the statement via `getSqlGetServicesStatement`, runs it
   * through the search executor, and serializes the result. A
   * present-but-empty result is answered with the literal `"[]"`.
   * @tparam T Client-derived entity type.
   * @param[in,out] entity Borrowed client entity carrying the id.
   * @param[in,out] callback Invoked once with 200 and the serialized services
   * (or a mapped error status).
   * @note The success paths live inside the `query.has_value()` branch: if
   * the entity produces no statement, this function issues no callback.
   * @see GetServicesAsync
   */
  template <typename T>
  void GetServices(T& entity, CALLBACK_&& callback)
    requires(std::is_base_of_v<Client, T>)
  {
    std::optional<jsoncons::json> services;  // Will hold the serialized service
                                             // list once the query returns.
    std::optional<std::string> query;  // Holds the built SQL; empty means the
                                       // entity produced nothing to run.

    try
    {
      pqxx::params params;  // The bag of values bound to the SQL
                            // placeholders ($1, $2, ...).
      query = entity.getSqlGetServicesStatement(
          params);  // Build the SELECT for this client's services.

      if (query.has_value())
      {
        services = databaseController
                       ->executeSearchQuery(query.value(),
                                            params);  // Blocking lookup of the
                                                      // service rows.

        if (services.has_value())
        {
          std::move(callback)(api::v2::Http::Status::OK,
                              api::v2::JsonHelper::stringify(services.value()));
          return;
        }
        std::move(callback)(api::v2::Http::Status::OK,
                            "[]");  // No services found still yields a valid
                                    // empty JSON array.
      }
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }

  /**
   * @brief Coroutine counterpart of GetServices().
   * @details Awaits `DatabaseController::executeSearchQueryCoro` on the
   * Drogon IO loop. The database controller is obtained through
   * `Store::cachedObject` (thread-local cache) rather than from the member
   * pointer, which is cheap enough for a per-request lookup.
   * @tparam T Client-derived entity type; must outlive the coroutine.
   * @param[in,out] entity Borrowed client entity carrying the id.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see GetServices, Store::cachedObject
   */
  template <typename T>
  drogon::Task<void> GetServicesAsync(T& entity, CALLBACK_&& callback)
    requires(std::is_base_of_v<Client, T>)
  {
    std::optional<jsoncons::json> services;  // Will hold the serialized service
                                             // list once the query returns.
    std::optional<std::string> query;  // Holds the built SQL; empty means the
                                       // entity produced nothing to run.

    try
    {
      pqxx::params params;  // The bag of values bound to the SQL
                            // placeholders ($1, $2, ...).
      query = entity.getSqlGetServicesStatement(
          params);  // Build the SELECT for this client's services.

      if (query.has_value())
      {
        auto db =
            Store::cachedObject<DatabaseController>();  // Thread-local cached
                                                        // handle: cheap
                                                        // per-request lookup
                                                        // for coroutines.
        services =
            co_await db->executeSearchQueryCoro(query.value(),
                                                params);  // Await the async
                                                          // service lookup.

        if (services.has_value())
        {
          std::move(callback)(api::v2::Http::Status::OK,
                              api::v2::JsonHelper::stringify(services.value()));
          co_return;
        }
        std::move(callback)(api::v2::Http::Status::OK,
                            "[]");  // No services found still yields a valid
                                    // empty JSON array.
      }
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }

  /**
   * @brief Retrieves a patient's visit history and returns it as a JSON
   * value.
   * @details Builds the statement via `getSqlGetVisitsStatement`, runs it
   * through the search executor and always answers with 200 (the
   * default-constructed JSON value is serialized when no statement is
   * produced).
   * @tparam T Must be exactly `Patient`, the only entity exposing visit
   * queries.
   * @param[in,out] entity Borrowed `Patient` entity carrying the id.
   * @param[in,out] callback Invoked once with 200 and the serialized visits
   * (or a mapped error status).
   * @see GetVisitsAsync
   */
  template <typename T>
  void GetVisits(T& entity, CALLBACK_&& callback)
    requires std::is_same<T, Patient>::value
  {
    jsoncons::json visits;  // Default JSON value; serialized even when no
                            // statement is produced.
    std::optional<std::string> query;

    try
    {
      pqxx::params params;  // The bag of values bound to the SQL
                            // placeholders ($1, $2, ...).
      query = entity.getSqlGetVisitsStatement(
          params);  // Build the SELECT for this patient's visits.

      if (query.has_value())
      {
        visits =
            databaseController
                ->executeSearchQuery(query.value(),
                                     params);  // Blocking lookup of visit rows.
      }

      std::move(callback)(api::v2::Http::Status::OK,
                          api::v2::JsonHelper::stringify(visits));
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }

  /**
   * @brief Coroutine counterpart of GetVisits().
   * @details Awaits `DatabaseController::executeSearchQueryCoro` on the
   * Drogon IO loop, using the thread-local cached database controller for the
   * per-request lookup.
   * @tparam T Must be exactly `Patient`; must outlive the coroutine.
   * @param[in,out] entity Borrowed `Patient` entity carrying the id.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see GetVisits, Store::cachedObject
   */
  template <typename T>
  drogon::Task<void> GetVisitsAsync(T& entity, CALLBACK_&& callback)
    requires std::is_same<T, Patient>::value
  {
    jsoncons::json visits;  // Default JSON value; serialized even when no
                            // statement is produced.
    std::optional<std::string> query;

    try
    {
      pqxx::params params;  // The bag of values bound to the SQL
                            // placeholders ($1, $2, ...).
      query = entity.getSqlGetVisitsStatement(
          params);  // Build the SELECT for this patient's visits.

      if (query.has_value())
      {
        auto db =
            Store::cachedObject<DatabaseController>();  // Thread-local cached
                                                        // handle: cheap
                                                        // per-request lookup
                                                        // for coroutines.
        visits =
            co_await db->executeSearchQueryCoro(query.value(),
                                                params);  // Await the async
                                                          // visit lookup.
      }

      std::move(callback)(api::v2::Http::Status::OK,
                          api::v2::JsonHelper::stringify(visits));
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }

 private:
  /**
   * Shared handle to the singleton database service; shared, not owned
   * exclusively, by each controller instance.
   */
  std::shared_ptr<DatabaseController>
      databaseController;  // Shared handle to the singleton DB service; not
                           // exclusively owned.
  /**
   * Member-function pointer selecting the write (non-read) executor used by
   * Create/Update/Delete and staff edits.
   */
  std::optional<jsoncons::json> (DatabaseController::*dbexec)(
      const std::string&, const pqxx::params&) =
      &DatabaseController::executeQuery;  // Selects the write executor for
                                          // Create/Update/Delete.
  /**
   * Member-function pointer selecting the read executor used by Read.
   */
  std::optional<jsoncons::json> (DatabaseController::*dbrexec)(
      const std::string&, const pqxx::params&) =
      &DatabaseController::executeReadQuery;  // Selects the read executor for
                                              // Read.

 protected:
  //
  /**
   * @brief Runs one entity statement: builds the parameterized SQL, executes
   * it and reports the result.
   * @details Invokes `(entity.*sqlstatement)(params)`; a missing statement
   * maps to 400. A present result that is non-empty maps to 200 with its JSON
   * serialization; empty or absent results map to 400 with a diagnostic body.
   * The chosen `func` decides whether this is a write or a read. Exceptions
   * are routed through `CRITICALMESSAGERESPONSE`, which answers 503 for
   * `DatabaseBusy` and 500 otherwise.
   * @tparam S Pointer-to-member type of the entity's SQL builder (deduced).
   * @tparam T Entity type (deduced); must outlive this call.
   * @param[in,out] entity Entity whose builder populates @p params.
   * @param[in] sqlstatement Pointer to the entity builder method to use.
   * @param[in] func Pointer to the `DatabaseController` executor method (e.g.
   * dbexec/dbrexec).
   * @param[in,out] callback Invoked exactly once on any returning path.
   * @note Shared implementation for every synchronous CRUDS helper.
   */
  template <typename S, typename T>
  void cruds(T& entity,
             S& sqlstatement,
             std::optional<jsoncons::json> (DatabaseController::*func)(
                 const std::string&, const pqxx::params&),
             CALLBACK_&& callback)
  {
    std::optional<jsoncons::json>
        results_j;  // Will hold the single result object returned by the DB
                    // helper.
    pqxx::params params;
    try
    {
      std::optional<std::string> query =
          (entity.*sqlstatement)(params);  // Call the chosen builder on the
                                           // entity; it also fills params.
      // A nullopt statement means the entity could not describe the
      // operation: reject before touching the DB.
      if (!query.has_value())
      {
        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            fmt::format("Failed to get SQL statement for {}.",
                                        entity.getTableName()));
        return;
      }

      // Dispatch through the selected executor; params are bound
      // placeholders, not string-interpolated values.
      results_j =
          (*databaseController.*
           func)(query.value(),
                 params);  // Run SQL through the chosen read/write executor.
      if (results_j.has_value())
      {
        if (!results_j->empty())
        {
          std::move(callback)(api::v2::Http::Status::OK,
                              results_j.value().as<std::string>());
          return;
        }

        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            "Query returned empty result, please recheck your "
                            "parameters.");
        return;
      }

      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                          "Failed to create sql query");
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }

  /**
   * @brief Coroutine implementation shared by every asynchronous CRUDS
   * helper.
   * @details Mirrors cruds(): builds the statement and params, awaits `func`
   * on the Drogon IO loop (the database controller is fetched from
   * `Store::cachedObject` for the per-request lookup), then maps
   *          present/non-empty → 200, missing/empty → 400. Every returning
   * path invokes @p callback exactly once.
   * @tparam S Pointer-to-member type of the entity's SQL builder (deduced).
   * @tparam T Entity type (deduced); must outlive the coroutine.
   * @param[in,out] entity Entity whose builder populates the parameter bag.
   * @param[in] sqlstatement Pointer to the entity builder method.
   * @param[in] func Pointer to an async `DatabaseController` executor
   * returning a `drogon::Task`.
   * @param[in,out] callback Invoked once on completion; moved into the
   * coroutine.
   * @see cruds
   */
  template <typename S, typename T>
  drogon::Task<void> crudsAsync(
      T& entity,
      S& sqlstatement,
      drogon::Task<std::optional<jsoncons::json>> (DatabaseController::*func)(
          const std::string&, const pqxx::params&),
      CALLBACK_&& callback)
  {
    std::optional<jsoncons::json>
        results_j;  // Will hold the single result object returned by the DB
                    // helper.
    pqxx::params params;
    try
    {
      std::optional<std::string> query =
          (entity.*sqlstatement)(params);  // Call the chosen builder on the
                                           // entity; it also fills params.
      if (!query.has_value())
      {
        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            fmt::format("Failed to get SQL statement for {}.",
                                        entity.getTableName()));
        co_return;
      }

      auto db = Store::cachedObject<DatabaseController>();
      results_j =
          co_await std::invoke(func,
                               db.get(),
                               query.value(),
                               params);  // Invoke the async executor member on
                                         // the cached DB object.
      if (results_j.has_value())
      {
        if (!results_j->empty())
        {
          std::move(callback)(api::v2::Http::Status::OK,
                              results_j.value().as<std::string>());
          co_return;
        }

        std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                            "Query returned empty result, please recheck your "
                            "parameters.");
        co_return;
      }

      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
                          "Failed to create sql query");
    }
    catch (const std::exception& e)
    {
      CRITICALMESSAGERESPONSE  // Log a critical error and answer through
                               // the callback with a mapped status.
    }
  }

  /**
   * @brief Adds a staff member via the entity's `getSqlAddStaffStatement`,
   * using the write executor.
   * @tparam T Entity type exposing the add-staff SQL builder.
   * @param[in,out] entity Borrowed entity whose builder binds the statement
   * parameters.
   * @param[in,out] callback Invoked once with the mapped status/body.
   * @see cruds
   */
  template <typename T>
  void addStaff(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlAddStaffStatement;  // Pointer to the builder that links a
                                      // staff member.
    cruds(entity, sqlstatement, dbexec, std::move(callback));
  }

  /**
   * @brief Removes a staff member via the entity's
   * `getSqlRemoveStaffStatement`, using the write executor.
   * @tparam T Entity type exposing the remove-staff SQL builder.
   * @param[in,out] entity Borrowed entity whose builder binds the statement
   * parameters.
   * @param[in,out] callback Invoked once with the mapped status/body.
   * @see cruds
   */
  template <typename T>
  void removeStaff(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlRemoveStaffStatement;  // Pointer to the builder that
                                         // unlinks a staff member.
    cruds(entity, sqlstatement, dbexec, std::move(callback));
  }

  /**
   * @brief Coroutine counterpart of addStaff(), awaiting
   * `DatabaseController::executeQueryCoro`.
   * @tparam T Entity type exposing the add-staff SQL builder; must outlive
   * the coroutine.
   * @param[in,out] entity Borrowed entity whose builder binds the statement
   * parameters.
   * @param[in,out] callback Invoked once on completion.
   * @see crudsAsync, addStaff
   */
  template <typename T>
  drogon::Task<void> addStaffAsync(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlAddStaffStatement;  // Pointer to the builder that links a
                                      // staff member.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::move(callback));
  }

  /**
   * @brief Coroutine counterpart of removeStaff(), awaiting
   * `DatabaseController::executeQueryCoro`.
   * @tparam T Entity type exposing the remove-staff SQL builder; must outlive
   * the coroutine.
   * @param[in,out] entity Borrowed entity whose builder binds the statement
   * parameters.
   * @param[in,out] callback Invoked once on completion.
   * @see crudsAsync, removeStaff
   */
  template <typename T>
  drogon::Task<void> removeStaffAsync(T& entity, CALLBACK_&& callback)
  {
    std::optional<std::string> (T::*sqlstatement)(pqxx::params&) =
        &T::getSqlRemoveStaffStatement;  // Pointer to the builder that
                                         // unlinks a staff member.
    co_await crudsAsync(entity,
                        sqlstatement,
                        &DatabaseController::executeQueryCoro,
                        std::move(callback));
  }
};
