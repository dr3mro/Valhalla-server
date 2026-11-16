/**
 * @file database.cpp
 * @brief Implementation for database: Takes ownership of a connection,
 * validates it and caches its connection string.
 * @details This file implements every helper declared in database.hpp. The
 * constructor opens and validates the connection,
 * checkExists()/executeQuery()/doSimpleQuery() run SQL, check_connection()
 * probes the link, reconnect() rebuilds it, and getTableSchema() /
 *          getAllTables() read catalog metadata. Failures are logged and turned
 * into "empty" return values rather than being allowed to crash the server.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the synchronous database path actually touches PostgreSQL.
 * Controllers reach it through a DatabaseHanndler lease obtained from
 * DatabaseConnectionPool, and the watchdog uses check_connection()/reconnect()
 * here to keep links healthy. The template functions at the bottom are
 * explicitly instantiated so other translation units can call them. Without
 * this file the declarations in database.hpp would have no implementation and
 * linking would fail, so no blocking database query could run.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (a transaction object commits or rolls back when it goes out of
 * scope): https://en.cppreference.com/w/cpp/language/raii
 * - std::shared_ptr and std::make_shared:
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - if constexpr (compile-time branching):
 * https://en.cppreference.com/w/cpp/language/if
 * - static_assert (a compile-time guarantee):
 * https://en.cppreference.com/w/cpp/language/static_assert
 * - std::string_view (a non-owning view of text):
 * https://en.cppreference.com/w/cpp/string/basic_string_view
 * - Function templates (the explicit instantiations at the bottom of this
 * file): https://en.cppreference.com/w/cpp/language/function_template
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations this file implements (Database and its members).
#include "database.hpp"

#include <fmt/core.h>  // Provides fmt::format used to compose log messages.
// Full format header; kept for the same formatting calls.
#include <fmt/format.h>
// Underlying logger used by the Message helpers.
#include <trantor/utils/Logger.h>

// Standard library utilities pulled in by the logging/new code paths.
#include <cstdlib>
// std::exception, the base of all exceptions caught in the catch blocks.
#include <exception>
// The jsoncons value type used to build the returned JSON objects.
#include <jsoncons/basic_json.hpp>
// jsoncons::json and its parse() helper for JSON/JSONB columns.
#include <jsoncons/json.hpp>
#include <memory>  // std::make_shared and std::shared_ptr used by reconnect().
// std::optional and std::nullopt used as the "maybe a value" returns.
#include <optional>
// std::string used for queries, cached connection info and column values.
#include <string>
// std::string_view holds a field name without copying it.
#include <string_view>
// std::is_same_v drives the compile-time checks in executeQuery().
#include <type_traits>
// std::unordered_set holds the schema and table-name results.
#include <unordered_set>
// std::move hands the connection pointer into the member without copying.
#include <utility>

// Shared controller declarations used by the API layer.
#include "controllers/base/controller/controller.hpp"
// api::v2::ColumnInfo, filled in by getTableSchema().
#include "utils/global/types.hpp"
// Message::{Critical,Info,Error}Message used for all logging here.
#include "message/message.hpp"

/**
 * Takes ownership of a connection, validates it and caches its connection
 * string. A failed validation is logged (critical) but does not throw; the
 * object still exists with the supplied pointer. reconnect() later depends on
 * the cached string. conn: Connection moved into this Database.
 */
Database::Database(std::shared_ptr<pqxx::connection>&& conn)
    // The initializer list stores the moved-in
    // connection before the body runs.
    : connection(std::move(conn)) {
  // Probing the connection can throw, so it is done inside a guarded
  // block.
  try {
    // A null pointer is tolerated: there is
    // nothing to validate in that case.
    if (connection != nullptr) {
      pqxx::nontransaction ntxn(
          *connection);  // A nontransaction runs a statement without
                         // starting a transaction; it is scoped to this
                         // block.

      ntxn.exec("SELECT 1");  // A trivial round trip that proves the link
                              // works and the server responds.

      // Cache the credentials so
      // reconnect() can rebuild the
      // link later.
      // A broken link is reported, but
      // construction still succeeds.
      connection_info = connection->connection_string();
    }
  } catch (const std::exception& e) {
    Message::CriticalMessage(
        fmt::format("Exception caught during database connection: {}",
                    e.what()));  // Log the exact driver error for diagnosis.
  }
}

/**
 * @brief Logs that this connection wrapper is being destroyed.
 */
Database::~Database() {
  Message::InfoMessage(
      fmt::format("Database connection {} is now exterminated.",
                  static_cast<const void*>(this)));
}  // Logging the address makes each connection's lifetime traceable.

/**
 * Checks whether a row exists using a parameterised EXISTS query.
 * Table and column are passed through `quote_name` (identifiers cannot be bound
 * as parameters); only the value is bound. The transaction scope is closed
 * before the result is read so no idle transaction is left open on the
 * connection. table: Table name. column: Column name. value: Value bound to
 * `$1`. Returns `true` if the row exists; `false` if not, or if the query
 * threw. Note: Query failures are swallowed and reported as `false` (see the
 * header note).
 */
bool Database::checkExists(const std::string& table,
                           const std::string& column,
                           const std::string& value) {
  // Any SQL error is converted into a false return below instead of
  // escaping the function.
  try {
    std::string query =
        fmt::format("SELECT EXISTS (SELECT 1 FROM {} WHERE {} = $1);",
                    connection->quote_name(table),
                    connection->quote_name(
                        column));  // quote_name escapes identifiers (which
                                   // cannot be bound as parameters); only the
                                   // value uses $1.
    // Empty result holder, kept alive after the
    // transaction object is destroyed.
    pqxx::result result;
    {
      pqxx::nontransaction txn(
          *connection);  // A short-lived, non-transactional execution
                         // context.

      result =
          txn.exec(pqxx::zview(query),
                   pqxx::params{value});  // Send the SQL with the value bound
                                          // separately, avoiding SQL injection.
    }
    // EXISTS always returns one
    // row/column: read it as a boolean.
    // A query failure is indistinguishable
    // from "not found" for callers.
    return result[0][0].as<bool>();
  } catch (const std::exception& e) {
    Message::CriticalMessage(
        fmt::format("Error executing query: {}",
                    e.what()));  // Still log it so the failure is visible in
                                 // the logs.
    return false;  // Report "not found" on error; use check_connection() to
                   // tell the cases apart.
  }
}

/**
 * @brief Probes the connection with a lightweight `SELECT 1` round trip.
 * @return `true` only if the pointer is non-null, the probe succeeded and the
 * socket is open.
 */
bool Database::check_connection() {
  // There is no connection to probe, so it cannot be healthy.
  if (connection == nullptr) {
    return false;  // Report unhealthy immediately.
  }
  // exec() throws if the link is broken; that means "unhealthy" rather
  // than a program error.
  try {
    pqxx::nontransaction ntxn(
        *connection);  // Minimal execution context for the probe.
    // If this completes, the server answered.
    // Any driver exception means the probe failed.
    ntxn.exec("SELECT 1");
  } catch (const std::exception& e) {
    return false;  // Signal that this link needs reconnecting.
  }
  return connection->is_open();  // Confirm the underlying socket is still
                                 // open as a final check.
}

/**
 * @brief Rebuilds the wrapped connection from the string cached at
 * construction.
 * @return The health check result of the new connection; `false` if creation or
 * check failed.
 * @details The old connection is dropped when the `shared_ptr` is reassigned.
 * The failure path is logged as critical. Not thread-safe: the caller must own
 * the Database exclusively (see DatabaseConnectionPool::reconnect_all and the
 * watchdog).
 * @pre check_connection() returned false, or a reconnect was otherwise
 * requested.
 */
bool Database::reconnect() {
  // Opening a new connection can throw (bad credentials, server down,
  // timeout).
  try {
    {
      connection = std::make_shared<pqxx::connection>(
          connection_info);  // Build a fresh connection from the cached
                             // string; the old one is released here.
    }
    // Only report success if the new link also
    // passes a real probe.
    // A failure leaves the old pointer in place; report it.
    return check_connection();
  } catch (const std::exception& e) {
    Message::CriticalMessage(
        fmt::format("Failed to reconnect to database,{}",
                    e.what()));  // Log the driver's reason for the reconnect
                                 // failure.
    return false;                // Tell the caller the reconnect did not work.
  }
}

/**
 * Reads the column metadata of a table, excluding the synthetic `id` column.
 * Maps `information_schema.columns` rows into ColumnInfo: a missing default
 * becomes the literal "None" and `is_nullable == "YES"` becomes `true`.
 * tableName: Table to inspect (bound as a parameter).
 * Returns A set of columns, or `std::nullopt` if the query threw.
 * Note: ColumnInfo's equality/hash ignore the Constraint field, so the set is
 * keyed by name, data type and nullability.
 */
std::optional<std::unordered_set<api::v2::ColumnInfo>> Database::getTableSchema(
    const std::string& tableName) {
  // Catalog queries can fail, so the whole read is guarded.
  try {
    pqxx::result result;  // Holds the returned metadata rows.

    {
      pqxx::nontransaction ntxn(
          *connection);  // A read-only statement needs no transaction.
      result =
          ntxn.exec(pqxx::zview(
                        "SELECT column_name, data_type, column_default, "
                        "is_nullable FROM "
                        "information_schema.columns WHERE table_name = $1 AND "
                        "column_name != 'id';"),  // information_schema is
                                                  // PostgreSQL's built-in
                                                  // catalog of
                    // table/column metadata; the id column is filtered out.
                    pqxx::params{
                        tableName});  // The table name is bound as a value, not
                                      // spliced into the SQL text.
    }

    std::unordered_set<api::v2::ColumnInfo>
        schema;  // Collect columns in a set so duplicates collapse
                 // automatically.

    for (const auto& row :
         // One iteration per column found in the catalog.
         result) {
      api::v2::ColumnInfo
          column;  // Fill a value object from the four selected fields.
      column.Name =
          row["column_name"].as<std::string>();  // Column name as text.
      column.DataType =
          row["data_type"].as<std::string>();  // Database type name, e.g.
                                               // "character varying".
      column.Constraint =
          row["column_default"].is_null()
              ? "None"
              : row["column_default"]
                    .as<std::string>();  // No default value becomes the
                                         // readable string "None".
      column.isNullable =
          row["is_nullable"].as<std::string>() ==
          "YES";  // Convert the catalog's "YES"/"NO" into a boolean.
      schema.insert(column);  // Add the finished column to the set.
    }
    // Return the complete set to the caller.
    // A failed query must not crash the caller.
    return schema;
  } catch (const std::exception& e) {
    Message::CriticalMessage(
        fmt::format("Error executing query: {}",
                    e.what()));  // Log the failure with the driver's message.
    return std::nullopt;         // Use the empty optional to signal "schema
                                 // unavailable".
  }
}

/**
 * @brief Lists all table names in the `public` schema.
 * @return A set of names, or `std::nullopt` if the query threw.
 * @details Uses a `pqxx::work` (committed implicitly when it goes out of scope)
 * even though the statement is read-only.
 */
std::optional<std::unordered_set<std::string>> Database::getAllTables() {
  // Guard against catalog query failures.
  try {
    pqxx::result result;  // Holds the table-name rows.

    {
      pqxx::work txn(*connection);  // A work transaction commits on scope exit;
                                    // harmless for a read-only statement.
      result = txn.exec(
          "SELECT table_name FROM information_schema.tables WHERE "
          "table_schema = 'public';");  // List only user tables in the
                                        // default "public" schema.
    }

    std::unordered_set<std::string>
        tables;  // A set removes duplicates and gives fast lookup.

    // One iteration per table name returned.
    for (const auto& row : result) {
      tables.insert(
          row["table_name"].as<std::string>());  // Copy the name into the set.
    }
    // Return all discovered table names.
    // A failed query must not crash the caller.
    return tables;
  } catch (const std::exception& e) {
    Message::CriticalMessage(fmt::format("Error executing query: {}",
                                         e.what()));  // Log the failure.
    return std::nullopt;  // Signal that the table list could not be read.
  }
}

/**
 * Executes a parameterised query and converts result rows to JSON.
 * Commits the transaction only when TransactionType is `pqxx::work`; a
 *          `pqxx::nontransaction` simply ends at scope exit. Columns are
 * converted by PostgreSQL type OID, with unhandled types falling back to a
 * string. Template parameter jsonType: `jsoncons::json` (returns the first row)
 * or `jsoncons::json::array` (returns all rows); any other type is rejected by
 * a static_assert. Template parameter TransactionType: Transaction wrapper used
 * for the execution. query: SQL text with `$1, $2, ...` placeholders. params:
 * Values bound to those placeholders. Returns The converted value, or
 * `std::nullopt` if any exception was caught. Note: For the single-object
 * specialisation an empty result yields a default-constructed JSON value, not
 * `std::nullopt` (only exceptions produce `std::nullopt`).
 */
template <typename jsonType,
          typename TransactionType>
std::optional<jsonType> Database::executeQuery(const std::string&  query,
                                               const pqxx::params& params) {
  static_assert(std::is_same_v<jsonType, jsoncons::json> ||
                    std::is_same_v<jsonType, jsoncons::json::array>,
                "Unsupported jsonType specialization");  // Fail at compile time
                                                         // if a caller passes
                                                         // an unsupported JSON
                                                         // shape.

  // Any conversion or SQL error becomes an empty optional below.
  try {
    pqxx::result results;  // Kept outside the transaction scope so rows
                           // survive after it closes.

    {
      TransactionType txn(
          *connection);  // Uses either a work or a nontransaction, chosen
                         // by the template argument.

      results = txn.exec(pqxx::zview(query),
                         params);  // Run the SQL with the parameters bound
                                   // separately.

      // Only a real transaction
      // needs an explicit commit;
      // this check happens at
      // compile time.
      if constexpr (std::is_same_v<TransactionType, pqxx::work>) {
        txn.commit();  // Make the writes permanent for work
                       // transactions.
      }
    }

    jsonType reply;  // Either a JSON array to fill, or a default JSON
                     // object for the single-row case.

    // Walk every row returned by the query.
    for (const auto& row : results) {
      // A fresh object per row, so no field can leak from one row into
      // the next.
      jsoncons::json object;  // Build one JSON object for this row.

      // Walk every column in the current row.
      for (const auto& field : row) {
        const std::string_view field_name =
            field.name();  // A view avoids copying the column name; it
                           // stays valid for the row's lifetime.
        const unsigned int field_type =
            field.type();  // PostgreSQL type OID, used below to pick
                           // the right conversion.

        // SQL NULL has to become JSON null, not
        // the string "null".
        if (field.is_null()) {
          object[field_name] = nullptr;  // Represent a SQL NULL explicitly in
                                         // the JSON output.
        } else {
          // Dispatch on the column's OID so
          // values keep their proper JSON type.
          switch (field_type) {
            case TEXT:  // TEXT or VARCHAR: database text becomes a
                        // JSON string.
              object[field_name] =
                  field.as<std::string>();  // Convert the field to a C++
                                            // string, which jsoncons
                                            // stores as a JSON string.
              break;                        // Move on to the next column.

            case INTEGER:  // INTEGER: keep it a number rather than
                           // a string.
              object[field_name] = field.as<int>();  // Convert to int so the
                                                     // JSON value is numeric.
              break;  // Move on to the next column.

            case BOOLEAN:  // BOOLEAN: keep the true/false meaning.
              object[field_name] = field.as<bool>();  // Convert to a C++ bool,
                                                      // which becomes a JSON
                                                      // boolean.
              break;  // Move on to the next column.

            case JSON:   // JSON: the field holds JSON text that
                         // should stay structured.
            case JSONB:  // JSONB: binary JSON, delivered as text by
                         // libpqxx, parsed the same way.
              object[field_name] = jsoncons::json::parse(
                  field.as<std::string>());  // Parse the text so
                                             // the result is a
                                             // nested JSON value,
                                             // not an escaped
                                             // string.
              break;                         // Move on to the next column.

            default:  // Handle unknown or unhandled types: fall
                      // back safely instead of throwing.
              object[field_name] = field.c_str();  // Default to string
              // representation, so no data is lost for unfamiliar
              // types.
              break;  // Move on to the next column.
          }
        }
      }
      // For the array
      // shape, accumulate
      // every row at
      // compile time.
      if constexpr (std::is_same_v<jsonType, jsoncons::json::array>) {
        reply.push_back(
            std::move(object));  // Transfer this row's object into the
                                 // result array without copying.
      } else {
        return object;  // For the single-object shape, return the first
                        // row immediately and ignore the rest.
      }
    }
    // For the array shape, return all collected rows after
    // the loop.
    // A SQL or conversion error must not
    // propagate to the caller.
    return reply;
  } catch (const std::exception& e) {
    Message::ErrorMessage(
        "Error executing query:");  // Print a short marker first.
    Message::InfoMessage(query);    // Log the exact SQL to make the failing
                                    // statement identifiable.
    Message::CriticalMessage(
        e.what());  // Log the driver/conversion error itself.
  }
  return std::nullopt;  // Signal failure with an empty optional.
}

/**
 * Executes a query in a nontransaction and returns a single scalar value.
 * Reads the first column of the first row; an empty result becomes
 * `std::nullopt`. Template parameter T: Target scalar type
 * (`as<std::optional<T>>()` maps SQL NULL to `std::nullopt`). query: SQL text
 * with placeholders. params: Values bound to those placeholders. Returns The
 * value, or `std::nullopt` on empty result or any caught exception.
 */
template <typename T>
std::optional<T> Database::doSimpleQuery(const std::string&  query,
                                         const pqxx::params& params) {
  // Guard against SQL and conversion errors.
  try {
    pqxx::result result;  // Holds rows after the transaction scope ends.

    {
      pqxx::nontransaction txn(
          *connection);  // A simple lookup needs no transaction.

      result = txn.exec(pqxx::zview(query),
                        params);  // Run the query with bound parameters.
    }

    return result.empty() ? std::nullopt
                          // No rows means no value;
                          // the optional conversion
                          // also turns a SQL NULL into
                          // nullopt.
                          // Convert any error into the empty result.
                          : result[0][0].as<std::optional<T>>();
  } catch (const std::exception& e) {
    Message::ErrorMessage("Error executing query:");  // Marker line in the log.
    Message::InfoMessage(query);         // The failing SQL statement.
    Message::CriticalMessage(e.what());  // The error text from libpqxx.
    return std::nullopt;                 // Report failure to the caller.
    // throw;  // Rethrow the exception to indicate failure
  }
}

// Explicit instantiations: these are the only supported specialisations of the
// templates defined above, provided here so callers link against them.
template std::optional<uint64_t> Database::doSimpleQuery<uint64_t>(
    const std::string&,
    const pqxx::params&);  // 64-bit integer lookups (ids, counts).
template std::optional<std::string> Database::doSimpleQuery<std::string>(
    const std::string&,
    const pqxx::params&);  // Text lookups.
template std::optional<jsoncons::json> Database::executeQuery<
    jsoncons::json,
    pqxx::nontransaction>(const std::string&,
                          const pqxx::params&);  // First row, no transaction.
template std::optional<jsoncons::json> Database::executeQuery<jsoncons::json,
                                                              pqxx::work>(
    const std::string&,
    const pqxx::params&);  // First row, committing writes.
template std::optional<jsoncons::json::array> Database::executeQuery<
    jsoncons::json::array,
    pqxx::nontransaction>(const std::string&,
                          const pqxx::params&);  // All rows, no transaction.
