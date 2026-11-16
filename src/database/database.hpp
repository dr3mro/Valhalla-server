/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file database.hpp
 * @brief Owns one synchronous PostgreSQL connection used by the synchronous
 * database path.
 * @details This header declares the Database class, a thin wrapper around one
 * libpqxx connection to a PostgreSQL server. It offers small helpers to check
 * whether a row exists, probe whether the link is still alive, reconnect after
 * a failure, run parameterised SQL and turn the returned rows into JSON, and
 * read table metadata.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the lowest-level building block of the synchronous ("blocking")
 * database path. DatabaseConnectionPool creates these objects and leases them
 * out one borrower at a time; DatabaseHanndler holds a lease for the length of
 * one request; controllers call the query helpers declared here to read and
 * write application data. Without this class the server would have no way to
 * talk to PostgreSQL synchronously, so every data-backed endpoint (login,
 * appointments, clinics, and so on) would stop working.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII: https://en.cppreference.com/w/cpp/language/raii
 * - std::shared_ptr (a reference-counted smart pointer):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - Move semantics: https://en.cppreference.com/w/cpp/utility/move
 * - Function templates:
 * https://en.cppreference.com/w/cpp/language/function_template
 * - std::optional (a value that may be missing):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - if constexpr (compile-time branching):
 * https://en.cppreference.com/w/cpp/language/if
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#ifndef DATABASE_HPP  // Include guard: without it, including this header twice
                      // would redefine the class below.
#define DATABASE_HPP  // Marks this header as already included for the rest of
                      // this translation unit.

// Used to build log/error strings with placeholders such as "{}".
#include <fmt/format.h>
// Drogon's logging infrastructure; used indirectly by the message helpers.
#include <trantor/utils/Logger.h>

// Provides fixed-width integer types such as std::uint16_t for the type-OID
// constants.
#include <cstdint>
// Core jsoncons template used to build and parse JSON values.
#include <jsoncons/basic_json.hpp>
// Declares the concrete jsoncons::json type returned by the query helpers.
#include <jsoncons/json.hpp>
// Provides std::shared_ptr and std::make_shared used to share the connection.
#include <memory>
// Provides std::optional, the "value or nothing" return type used by every
// helper.
#include <optional>
// The libpqxx C++ client library: connection, nontransaction, work, result and
// params.
#include <pqxx/pqxx>
// std::string is used for table names, queries, values and cached connection
// info.
#include <string>
// std::unordered_set holds schema/table names with no duplicates.
#include <unordered_set>

// Brings in api::v2::ColumnInfo, the value type returned by getTableSchema().
#include "utils/global/types.hpp"

/**
 * @class Database
 * @brief Owns one synchronous PostgreSQL connection used by the synchronous
 * database path.
 * @details A Database wraps a single `pqxx::connection` and offers the query
 * helpers used by controllers through a DatabaseHanndler lease. Instances are
 * constructed and owned by DatabaseConnectionPool, which hands them out one
 * borrower at a time; callers never construct one directly. The constructor
 * validates the connection with `SELECT 1` and caches its connection string so
 * reconnect() can rebuild it later.
 *
 *          Thread-safety: the wrapped connection is used by one borrower at a
 * time as enforced by the pool, so no internal locking is performed.
 * Non-copyable and non-movable: ownership stays with the pool's
 * `std::shared_ptr`.
 * @note The constructor tolerates a null connection (no validation performed
 * and an empty cached connection string); in normal operation the pool passes a
 * validated connection.
 * @see DatabaseConnectionPool, DatabaseHanndler
 */
class Database
{
 public:
  // Non-copyable/non-movable: the pool owns the shared_ptr, borrowers only
  // hold a reference to it.
  Database(const Database&) = delete;  // Copying would create a second owner
                                       // of the same live connection.
  Database(Database&&) =
      delete;  // Moving would let a borrower steal the pool's connection.
  Database& operator=(const Database&) =
      delete;  // Copy-assignment would violate single ownership too.
  Database& operator=(Database&&) =
      delete;  // Move-assignment would violate single ownership too.

  /**
   * @brief Takes ownership of a pqxx connection and validates it.
   * @details Runs `SELECT 1` in a nontransaction when the pointer is non-null
   * and caches `connection_string()` for later use by reconnect(). A
   * validation failure is logged as critical but does not throw; the object
   * still exists.
   * @param[in,out] conn Connection to take ownership of (moved from the
   * caller).
   */
  explicit Database(std::shared_ptr<pqxx::connection>&&
                        conn);  // `explicit` prevents an accidental implicit
                                // conversion from a bare pointer.

  /**
   * @brief Destroys the wrapper, logging that the connection is being
   * released.
   */
  virtual ~Database();  // Virtual so the pool can safely destroy a Database
                        // through a base-class pointer if needed.

  /**
   * @brief Returns whether a row exists in a table for the given column
   * value.
   * @details Builds `SELECT EXISTS (...)` using `quote_name` for the table
   * and column to prevent SQL injection, binding only the value as a
   * parameter.
   * @param[in] table Table name (identifier, quoted).
   * @param[in] column Column name (identifier, quoted).
   * @param[in] value Value bound to the `$1` placeholder.
   * @return `true` if the row exists; `false` if it does not or if the query
   * threw.
   * @note A query error is indistinguishable from "not found" here (both
   * return `false`); use check_connection() when that distinction matters.
   */
  bool checkExists(
      const std::string& table,
      const std::string& column,
      const std::string& value);  // Returns true only when at least one
                                  // matching row was found.

  /**
   * @brief Checks whether the wrapped connection is open and responds to
   * `SELECT 1`.
   * @return `true` only when the pointer is non-null, the probe succeeded and
   * `is_open()`.
   */
  bool check_connection();  // Used by the watchdog to decide whether this
                            // link needs reconnecting.

  /**
   * @brief Replaces the wrapped connection with a fresh one built from the
   * cached string.
   * @return The result of check_connection() on the new connection; `false`
   * on failure.
   * @note Not thread-safe: callers must ensure no query is in flight. The old
   * connection is released when the `shared_ptr` is replaced.
   */
  bool reconnect();  // Recreates the connection from the string cached in the
                     // constructor.

  // The statement uses $1, $2, ... placeholders; the values travel separately
  // in params. This separation is what keeps user-supplied values from being
  // interpreted as SQL code.
  /**
   * @brief Executes a parameterised query and converts the result rows to
   * JSON.
   * @details Runs the statement through TransactionType (committed only when
   * it is a `pqxx::work`). For a json array every result row is appended; for
   * a single json object only the first row is returned.
   * @tparam jsonType `jsoncons::json` (first row) or `jsoncons::json::array`
   * (all rows).
   * @tparam TransactionType Transaction wrapper, e.g. `pqxx::nontransaction`
   * or `pqxx::work`.
   * @param[in] query SQL text with `$1, $2, ...` placeholders.
   * @param[in] params Values bound to those placeholders.
   * @return The converted value, or `std::nullopt` if any exception was
   * caught.
   */
  template <
      typename jsonType,
      typename TransactionType>  // One template serves both "first row" and
                                 // "all rows" and both transaction kinds.
  std::optional<jsonType> executeQuery(
      const std::string&  query,
      const pqxx::params& params = {});  // The empty default lets
                                         // callers run queries with no
                                         // parameters.

  /**
   * @brief Executes a query in a nontransaction and returns the first column
   * of the first row.
   * @details Uses `as<std::optional<T>>()` so a SQL NULL becomes
   * `std::nullopt`.
   * @tparam T Target scalar type for the returned value.
   * @param[in] query SQL text with placeholders.
   * @param[in] params Values bound to those placeholders.
   * @return The value, or `std::nullopt` if the result is empty or an
   * exception occurred.
   */
  template <typename T>  // T is the C++ type to convert the single returned
                         // column into.
  std::optional<T> doSimpleQuery(
      const std::string&  query,
      const pqxx::params& params = {});  // Used for simple one-value lookups
                                         // such as a count or an id.

  /**
   * @brief Reads the column metadata of a table from
   * `information_schema.columns`.
   * @param[in] tableName Table to inspect.
   * @return A set of ColumnInfo (the `id` column is excluded), or
   * `std::nullopt` on error.
   */
  std::optional<std::unordered_set<api::v2::ColumnInfo>> getTableSchema(
      const std::string& tableName);  // A set avoids duplicate column entries.

  /**
   * @brief Lists the table names in the `public` schema.
   * @return A set of table names, or `std::nullopt` on error.
   */
  std::optional<std::unordered_set<std::string>>
  getAllTables();  // Used by the database-schema validator to enumerate
                   // tables.

 private:
  /**
   * The borrowed pqxx connection; lifetime is shared with the pool.
   */
  std::shared_ptr<pqxx::connection>
      connection;  // The live link to PostgreSQL. shared_ptr keeps it alive
                   // as long as anyone still uses it.
  std::string connection_info;  // Store connection parameters: a copy of the
                                // connection string so reconnect() can build
                                // a fresh link.

  /**
   * PostgreSQL type OIDs used to dispatch a result column to the right JSON
   * conversion.
   */
  static const std::uint16_t TEXT =
      1043;  // OID for VARCHAR/TEXT: convert the field to a JSON string.
  static const std::uint16_t INTEGER =
      23;  // OID for INTEGER: convert the field to a JSON number.
  static const std::uint16_t BOOLEAN =
      16;  // OID for BOOLEAN: convert the field to a JSON true/false.
  static const std::uint16_t JSON =
      114;  // OID for JSON: parse the text into a nested JSON value.
  static const std::uint16_t JSONB =
      3802;  // OID for binary JSON: same parsing as JSON.
};

#endif  // DATABASE_HPP: end of the include guard opened at the top of the file.
