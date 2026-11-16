/**
 * @file asyncresultmapper.hpp
 * @brief Maps Drogon PostgreSQL result sets to jsoncons values with the
 * outcomes of the synchronous path in mind.
 * @details This header declares functions that convert query results into JSON.
 * One function converts a single row into a JSON object, and two helpers wrap
 * that for the common cases of "first row only" and "all rows as an array".
 * Column names and the value text decide the JSON type: nulls stay null, a
 * boolean flag becomes true/false, identifier columns become numbers,
 * JSON-looking text is parsed, and everything else is a string.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * DatabaseController's coroutine methods return JSON to the HTTP layer, but
 * Drogon gives them a Result set of rows and fields. These functions are the
 * translation step in between. They deliberately reproduce the behaviour of the
 * older synchronous Database::executeQuery so clients cannot tell which
 * database path answered their request. A malformed number or JSON value falls
 * back to plain text instead of failing the whole request, which keeps one bad
 * database row from breaking an entire API response. Without this file the
 * async controllers would have no way to produce the JSON shapes the rest of
 * the server expects.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Drogon DbClient and result sets:
 * https://drogon.docsforge.com/master/database/
 * - jsoncons JSON values: https://danielaparker.github.io/jsoncons/
 * - PostgreSQL data types and type OIDs:
 * https://www.postgresql.org/docs/current/datatype.html
 * - C++20 coroutines: https://en.cppreference.com/w/cpp/language/coroutines
 * - std::string_view (a non-owning view of text):
 * https://en.cppreference.com/w/cpp/string/basic_string_view
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include only once per translation unit, so these declarations
              // are never repeated.

// Result, the Drogon type holding the rows returned by a query.
#include <drogon/orm/Result.h>

// The underlying jsoncons template used for JSON values.
#include <jsoncons/basic_json.hpp>
// jsoncons::json, the JSON object/value type returned by these functions.
#include <jsoncons/json.hpp>
// The "value or nothing" type used by sibling async headers for nullable
// values; kept here for consistency.
#include <optional>
#include <string>  // std::string, used for column names and value text.

/**
 * @namespace asyncdb
 * @brief Maps Drogon PostgreSQL result sets to jsoncons values with the
 * outcomes of the synchronous path in mind.
 * @details SQL nulls become JSON null; the `active` flag becomes a boolean;
 * identifier-like columns become numbers when their text parses as an integer;
 * JSON-looking values are parsed; anything else stays a string. The mapping is
 *          based on column names and value text rather than on PostgreSQL type
 * OIDs — see mapRow.
 * @see Database::executeQuery, mapRow
 */
namespace asyncdb
{
  // Type OIDs mirrored from Database::executeQuery. mapRow infers types from
  // column names and value text, so these constants are unused in this
  // repository; no reason for retaining them is recorded. An OID is
  // PostgreSQL's numeric identifier for a data type. These are kept as
  // documentation of the types the synchronous mapper cares about; the async
  // mapper does not read the OID of a field.
  /** @brief varchar type OID. */
  static constexpr unsigned int PG_TEXT_OID =
      1043;  // varchar (treated as text); `constexpr` fixes the value at
             // compile time, `static` gives it internal linkage.
  /** @brief int4 (INTEGER) OID. */
  static constexpr unsigned int PG_INTEGER_OID =
      23;  // 23 is PostgreSQL's identifier for a 32-bit integer column.
  /** @brief bool (BOOLEAN) OID. */
  static constexpr unsigned int PG_BOOLEAN_OID =
      16;  // 16 is PostgreSQL's identifier for a boolean column.
  /** @brief json OID. */
  static constexpr unsigned int PG_JSON_OID =
      114;  // 114 identifies the JSON text type.
  /** @brief jsonb OID. */
  static constexpr unsigned int PG_JSONB_OID =
      3802;  // 3802 identifies the binary JSON type.

  /**
   * @brief Maps one result row to a JSON object keyed by column name.
   * @details Conversion order: SQL NULL to JSON null; a column named `active`
   * to a boolean (`t`, `true` or `1` mean true); `id`, names ending in `_id`,
   * `appointment_duration` and `appointment_payment_amount` to an integer
   * when `std::stoi` succeeds; a non-empty value starting with `{` or `[` to
   * parsed JSON; otherwise a string. Both integer and JSON parsing failures
   * fall back to the raw string, so a malformed value cannot abort mapping.
   *          A fresh object is built for each call, so no field can leak
   * between rows.
   * @param[in] result Result set returned by the query.
   * @param[in] row Zero-based row index into `result`.
   * @return JSON object with one entry per column, in column order.
   * @pre `row` is a valid index into `result`.
   */
  [[nodiscard]] jsoncons::json mapRow(
      const drogon::orm::Result& result,
      drogon::orm::Result::SizeType
          row);  // `[[nodiscard]]` reminds callers that ignoring the returned
                 // object is a mistake.

  /**
   * @brief Maps the first row of a result to a JSON object.
   * @param[in] result Result set returned by the query.
   * @return `mapRow(result, 0)` when non-empty; a default-constructed (null)
   * JSON value when empty.
   * @note This mirrors the single-object outcome of `Database::executeQuery`
   * for a query with no rows.
   */
  [[nodiscard]] jsoncons::json mapResultToObject(
      const drogon::orm::Result&
          result);  // Used when a query is expected to return at most one
                    // row, such as a lookup by id.

  /**
   * @brief Maps every row of a result to a JSON array, preserving row order.
   * @param[in] result Result set returned by the query.
   * @return Array with one JSON object per row; empty when the result set is
   * empty.
   */
  [[nodiscard]] jsoncons::json::array mapResultToArray(
      const drogon::orm::Result& result);  // Used for search/list queries that
                                           // must return every matching row.
}  // namespace asyncdb

/** @brief Aliases for the Phase 2 vocabulary; all denote the same namespace as
 * `asyncdb`. */
namespace ResultMapper =
    asyncdb;  // Short alias for callers that only need the mapping helpers.
namespace AsyncDbResultMapper =
    asyncdb;  // Alias matching the "async" naming used during the migration.
namespace DbResultMapper = asyncdb;  // Another compatibility alias pointing to
                                     // exactly the same namespace.
