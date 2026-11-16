/**
 * @file fake_db.hpp
 * @brief A fake database controller plus a guard that installs it, so unit
 * tests need no real database.
 * @details The fake answers every database method from simple member fields
 * instead of talking to PostgreSQL. Tests set the field for the result they
 * want and can ask the fake to throw a "busy" error or count how many queries
 * ran. The guard swaps the fake into the global Store for a test and restores
 * the real object afterwards. Synchronous and coroutine (async) methods share
 * the same result fields, so one fake can drive both code paths.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers normally read and write through DatabaseController. Without a
 * fake, testing them would require a live PostgreSQL instance, making tests
 * slow, fragile and dependent on machine setup. This file keeps tests hermetic
 * (self-contained) and lets them trigger edge cases — like a busy pool or a
 * failed query — that would be awkward to reproduce against a real database.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII guard (install something, undo it in the destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Coroutines (functions that can pause and resume):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - `override` (replacing a virtual function from a base class):
 * https://en.cppreference.com/w/cpp/language/override
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

// The JSON value type returned by the fake's query methods.
#include <jsoncons/json.hpp>
// std::shared_ptr and std::move, used to swap objects in the Store.
#include <memory>
#include <optional>  // std::optional, used for results that may be absent.
// pqxx::params, the parameter type in the overridden method signatures.
#include <pqxx/pqxx>
#include <string>         // std::string for SQL text and names.
#include <unordered_set>  // Used for the set-based schema/table results.

// The real class whose virtual methods we replace.
#include "controllers/databasecontroller/databasecontroller.hpp"
// DatabaseBusy, the exception thrown when the fake is "busy".
#include "database/databasebusy.hpp"
// Drogon's coroutine/Task support for the async methods.
#include "drogon/utils/coroutine.h"
// Store, the global registry the guard temporarily changes.
#include "store/store.hpp"
#include "utils/global/types.hpp"  // Shared types such as ColumnInfo.

/**
 * Hermetic replacement for DatabaseController in unit tests.
 *
 * Inject via FakeDbGuard (below) before constructing any controller that
 * captures databaseController from Store. All methods return the value set
 * in the corresponding public field (nullopt by default).
 * @brief A stand-in database object whose answers are set directly by the test.
 * @details Each query method just returns the matching `*_result` field (empty
 * unless the test fills it). This avoids a real database while still letting
 * tests observe results and call counts.
 */
class FakeDatabaseController : public DatabaseController
{
 public:
  std::optional<jsoncons::json>
      query_result;  // Answer returned by executeQuery (write queries).
  std::optional<jsoncons::json>
      read_result;  // Answer returned by executeReadQuery.
  std::optional<jsoncons::json::array>
      search_result;  // Answer returned by executeSearchQuery (a list of
                      // rows).
  std::optional<std::string>
      do_read_result;  // Answer returned by doReadQuery (raw text).
  std::optional<bool> exists_result;  // Answer returned by checkItemExists.
  std::optional<jsoncons::json>
      password_result;  // Answer returned by getPasswordHashForUserName.
  std::optional<uint64_t>
      find_user_result;  // Stored user id, available to tests that need one.
  std::optional<std::unordered_set<api::v2::ColumnInfo>>
      schema_result;  // Columns returned by getTableSchema.
  std::optional<std::unordered_set<std::string>>
      tables_result;  // Table names returned by getAllTables.
  std::optional<jsoncons::json>
      permissions_result;  // Permissions returned by getPermissions.

  /**
   * When true, the row/permission query methods throw DatabaseBusy (like a
   * pool whose wait timed out). Schema/metadata lookups (getTableSchema,
   * getAllTables) deliberately ignore this flag and still answer from their
   * result fields.
   */
  bool busy = false;  // Set to true to simulate a database pool that cannot
                      // serve the query right now.

  /**
   * How many times a write-capable query (executeQuery) ran, to check that a
   * path did not write.
   */
  int query_calls =
      0;  // Counter used to prove whether a code path performed a write.

  std::optional<jsoncons::json> executeQuery(const std::string&,
                                             const pqxx::params& = {})
      override  // Write query: may throw if busy, then counts the call and
                // returns query_result.
  {
    throwIfBusy();        // Simulate a busy pool by throwing before doing any
                          // work.
    ++query_calls;        // Record that a write was attempted.
    return query_result;  // Hand back whatever the test configured.
  }

  std::optional<jsoncons::json> executeReadQuery(
      const std::string&,
      const pqxx::params& = {}) override  // Read query: may throw if busy,
                                          // otherwise returns read_result.
  {
    throwIfBusy();       // Keep busy behaviour consistent across query methods.
    return read_result;  // Return the configured read answer.
  }

  std::optional<jsoncons::json::array> executeSearchQuery(
      const std::string&, const pqxx::params& = {})
      override  // Search query: returns a list of rows.
  {
    throwIfBusy();         // Simulate a busy pool here too.
    return search_result;  // Return the configured list of rows.
  }

  std::optional<std::string> doReadQuery(const std::string&,
                                         const pqxx::params& = {})
      override  // Raw read: returns plain text rather than JSON.
  {
    throwIfBusy();          // Same busy simulation.
    return do_read_result;  // Return the configured text answer.
  }

  std::optional<bool> checkItemExists(const std::string&,
                                      const std::string&,
                                      const std::string&)
      override  // Existence check: answers yes/no from a field.
  {
    throwIfBusy();         // Same busy simulation.
    return exists_result;  // Return the configured yes/no answer.
  }

  std::optional<jsoncons::json> getPasswordHashForUserName(
      const std::string&,
      const std::string&) override  // Looks up a stored password hash.
  {
    throwIfBusy();           // Same busy simulation.
    return password_result;  // Return the configured hash.
  }

  std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(
      const std::string&) override
  {
    return schema_result;
  }  // Metadata lookup: ignores `busy` on purpose.

  std::optional<std::unordered_set<std::string>> getAllTables() override
  {
    return tables_result;
  }  // Metadata lookup: ignores `busy` on purpose.

  std::optional<jsoncons::json> getPermissions(
      const std::string&) override  // Permission lookup for a table.
  {
    throwIfBusy();  // A permissions query is treated like a normal query.
    return permissions_result;  // Return the configured permissions.
  }

  // ---- Asynchronous (coroutine) seam for DB_ASYNC=true paths ----
  // The coroutine overrides answer from the same fields as their synchronous
  // counterparts, so one fake drives both paths hermetically.
  int coro_calls =
      0;  // how many async queries ran, to check a path used the async seam

  drogon::Task<std::optional<jsoncons::json>> executeQueryCoro(
      const std::string&, const pqxx::params& = {})
      override  // Async write query: mirrors executeQuery.
  {
    throwIfBusy();           // Simulate a busy pool in the async path too.
    ++query_calls;           // Count it as a write, just like the synchronous
                             // version.
    ++coro_calls;            // Also count that the async seam was used.
    co_return query_result;  // co_return yields the result from a
                             // coroutine.
  }

  drogon::Task<std::optional<jsoncons::json>> executeReadQueryCoro(
      const std::string&,
      const pqxx::params& = {}) override  // Async read query.
  {
    throwIfBusy();          // Same busy simulation.
    ++coro_calls;           // Record use of the async seam.
    co_return read_result;  // Return the configured read answer.
  }

  drogon::Task<std::optional<jsoncons::json::array>> executeSearchQueryCoro(
      const std::string&,
      const pqxx::params& = {}) override  // Async search query.
  {
    throwIfBusy();            // Same busy simulation.
    ++coro_calls;             // Record use of the async seam.
    co_return search_result;  // Return the configured list of rows.
  }

  drogon::Task<std::optional<std::string>> doReadQueryCoro(
      const std::string&,
      const pqxx::params& = {}) override  // Async raw-text read.
  {
    throwIfBusy();             // Same busy simulation.
    ++coro_calls;              // Record use of the async seam.
    co_return do_read_result;  // Return the configured text answer.
  }

  drogon::Task<std::optional<bool>> checkItemExistsCoro(
      const std::string&,
      const std::string&,
      const std::string&) override  // Async existence check.
  {
    throwIfBusy();            // Same busy simulation.
    ++coro_calls;             // Record use of the async seam.
    co_return exists_result;  // Return the configured yes/no answer.
  }

  drogon::Task<std::optional<jsoncons::json>> getPasswordHashForUserNameCoro(
      const std::string&,
      const std::string&) override  // Async password lookup.
  {
    throwIfBusy();              // Same busy simulation.
    ++coro_calls;               // Record use of the async seam.
    co_return password_result;  // Return the configured hash.
  }

  drogon::Task<std::optional<jsoncons::json>> getPermissionsCoro(
      const std::string&) override  // Async permission lookup.
  {
    throwIfBusy();                 // Same busy simulation.
    ++coro_calls;                  // Record use of the async seam.
    co_return permissions_result;  // Return the configured permissions.
  }

 private:
  void throwIfBusy() const  // One shared helper so every query method behaves
                            // the same when busy.
  {
    if (busy)  // Only throw when the test has asked for busy behaviour.
    {
      throw DatabaseBusy();  // Throw the same exception a real timed-out
                             // pool would throw.
    }
  }
};

/**
 * RAII guard: injects a fake DatabaseController into the Store for the
 * lifetime of the guard, then restores the original on destruction.
 *
 * Construct BEFORE creating any controller whose constructor calls
 * Store::getObject<DatabaseController>().
 * @brief Temporarily replaces the Store's database object with a fake.
 */
struct FakeDbGuard
{
  std::shared_ptr<DatabaseController>
      original;  // Keeps the real object so it can be restored later.

  explicit FakeDbGuard(
      std::shared_ptr<FakeDatabaseController>
          fake)  // `explicit` prevents accidental implicit conversion.
  {
    original =
        Store::getObject<DatabaseController>();  // Save whatever object is
                                                 // currently registered.
    Store::replaceObject<DatabaseController>(
        std::move(fake));  // Install the fake in its place.
  }

  ~FakeDbGuard()
  {
    Store::replaceObject<DatabaseController>(original);
  }  // Put the original object back at scope exit.

  FakeDbGuard(const FakeDbGuard&) =
      delete;  // No copying: there must be a single
               // owner of the restore action.
  FakeDbGuard& operator=(const FakeDbGuard&) =
      delete;  // No copy assignment, for the same reason.
};
