/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file asyncresultmapper.cpp
 * @brief Implementation for asyncresultmapper: Converts one row into a JSON
 * object using name- and value-based type inference.
 * @details This file implements the row-to-JSON conversion. For each column it
 * checks whether the value is SQL NULL, whether the column is a known boolean
 * or identifier field, whether the text looks like JSON, and otherwise keeps
 * the text as a string. The first-row and all-row helpers simply call the row
 * mapper in a loop or at index zero.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Async controller methods receive a Drogon Result and must return JSON to the
 * HTTP layer. This file performs that translation and deliberately mirrors the
 * older synchronous Database::executeQuery outcomes: nulls stay null, the
 * active flag is a boolean, ids are numbers, and json/jsonb columns become
 * nested JSON. Because parsing errors are caught and the raw text is kept, one
 * unusual database value cannot crash a whole list response. Without this file,
 * the async database path could not produce the same JSON contract that the
 * rest of the API relies on.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Drogon result sets, rows and fields:
 * https://drogon.docsforge.com/master/database/
 * - jsoncons JSON values and parsing: https://danielaparker.github.io/jsoncons/
 * - std::string_view (a cheap, non-owning look at text):
 * https://en.cppreference.com/w/cpp/string/basic_string_view
 * - C++ exceptions and catch-all handlers:
 * https://en.cppreference.com/w/cpp/language/try_catch
 * - C++20 coroutines: https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Our own declarations, verified against these definitions.
#include "database/asyncresultmapper.hpp"

// Field, a single cell value, with isNull() and c_str().
#include <drogon/orm/Field.h>
// Result, the whole set of rows returned by a query.
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>  // Row, one record inside a Result.
// Formatting library; this file makes no fmt:: calls itself (kept for
// consistency with sibling files).
#include <fmt/core.h>

#include <cstddef>  // Size types used for column and row indexes.
#include <jsoncons/basic_json.hpp>  // The underlying JSON value template.
#include <jsoncons/json.hpp>        // jsoncons::json and its parse() function.
// std::string, the owned copy of a column name or field value.
#include <string>

// Project logging helpers, kept available for diagnostics; this file itself
// does not log.
#include "utils/message/message.hpp"

namespace asyncdb
{
  /**
   * @brief Converts one row into a JSON object using name- and value-based
   * type inference.
   * @details Each row gets its own object. Parse failures are swallowed and
   * the raw text is kept, so this function never throws for a malformed
   * integer or JSON value. Field values are read through their C-string text
   * form.
   */
  jsoncons::json mapRow(
      const drogon::orm::Result& result, drogon::orm::Result::SizeType row)
  {
    jsoncons::json object;  // A fresh, empty JSON object for this row;
                            // nothing from another row can leak into it.

    const auto     column_count =
        result.columns();  // How many columns this result has, so we know
                           // how far to walk.
    for (drogon::orm::Result::RowSizeType i = 0; i < column_count;
        ++i)  // Visit each column exactly once, from left to right; stop
              // after the last column.
    {
      const std::string_view field_name =
          result.columnName(i);  // A non-owning view of the column's name, used
                                 // for comparisons and as the JSON key.
      const auto field = result[row][i];  // The single cell at (row, column):
                                          // its value and null flag.

      if (field.isNull())  // SQL NULL must become JSON null, not the
                           // string "NULL" or an empty string.
      {
        object[std::string(field_name)] =
            nullptr;  // Copy the name into an owning string for the
                      // key, then store a JSON null value.
        continue;     // Nothing else can be inferred from a null value, so
                      // move on to the next column.
      }

      const std::string name(
          field_name);  // Own the column name now, so it can be compared
                        // and reused as a JSON key safely.
      const std::string value(
          field.c_str());  // Read the cell as text; copying it keeps the
                           // data valid after the Result is touched again.

      if (name == "active")  // The active flag is known to be a boolean,
                             // whatever text PostgreSQL used.
      {
        // PostgreSQL emits t/f for booleans; accept the common truthy
        // spellings too.
        object[name] =
            (value == "t" || value == "true" ||
                value == "1");  // The comparison produces a C++ bool, which
                                // jsoncons stores as JSON true or false.
      }
      else if (name == "id" || name.ends_with("_id") ||
               name == "appointment_duration" ||
               name == "appointment_payment_amount")  // These column names
                                                      // mark values that the
                                                      // API exposes as whole
                                                      // numbers.
      {
        // These columns are numbers in the API, but keep the text if it
        // does not parse.
        try
        {
          object[name] =
              std::stoi(value);  // Convert the text to an integer; stoi throws
                                 // if the text is not a valid number.
        }
        catch (...)  // Catch every exception type from the conversion,
                     // including out-of-range numbers.
        {
          object[name] = value;  // Fall back to the original text so a single
                                 // odd value cannot abort the row.
        }
      }
      else if (!value.empty() &&
               (value.front() == '{' ||
                   value.front() ==
                       '['))  // A value starting with a brace or bracket
                              // is probably a JSON object or array.
      {
        // json/jsonb arrive as text; a failed parse degrades to a plain
        // string.
        try
        {
          object[name] =
              jsoncons::json::parse(value);  // Parse the text into real nested
                                             // JSON when it is well formed.
        }
        catch (...)  // Any parse error is deliberately ignored here.
        {
          object[name] = value;  // Keep the raw text as a string instead of
                                 // failing the whole request.
        }
      }
      else  // No special rule matched, so this is an ordinary text
            // column.
      {
        object[name] = value;  // Store the value as a plain JSON string.
      }
    }

    return object;  // The caller receives one JSON entry per column, in
                    // column order.
  }

  /**
   * @brief Maps the first row, using a null JSON value to represent an empty
   * result.
   * @details The emptiness check also avoids indexing into a result with no
   * rows.
   */
  jsoncons::json mapResultToObject(const drogon::orm::Result& result)
  {
    if (result.empty())  // A SELECT can legitimately return no rows, e.g. a
                         // lookup for a missing id.
    {
      return jsoncons::json();  // A default-constructed jsoncons::json is
                                // a JSON null, matching the synchronous
                                // path.
    }
    return mapRow(result,
        0);  // Row indexes start at zero, so this converts the first (and
             // possibly only) row.
  }

  /**
   * @brief Maps rows sequentially into an array, preserving the query's row
   * order.
   */
  jsoncons::json::array mapResultToArray(const drogon::orm::Result& result)
  {
    jsoncons::json::array
        reply;  // Empty array that will hold one JSON object per row.

    for (drogon::orm::Result::SizeType row = 0; row < result.size();
        ++row)  // Walk rows from first to last; stop when every row has
                // been converted.
    {
      reply.push_back(mapRow(result,
          row));  // Convert the row and append it, keeping the database's
                  // order intact.
    }
    return reply;  // An empty result simply produces an empty array, which
                   // is what list endpoints return.
  }
}  // namespace asyncdb
