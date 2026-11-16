/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file validator.cpp
 * @brief Implementation of the Validator checks declared in validator.hpp.
 * @details This file defines every function the header announces: the create,
 * update and read validators, the per-field regular-expression validation, and
 * the small private helpers that detect empty input, look up the cached schema,
 * compare data types and walk through columns and keys. They all work on an
 * in-memory schema snapshot and report problems by filling an
 * api::v2::Http::Error with an HTTP status code and a message.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the validation rules actually run. Entity controllers call the
 * public validateDatabase* functions before they build any SQL, and those
 * functions lean on the private helpers here and on the cached schema owned by
 * DatabaseSchema. Without this file the validators would be declarations only,
 * and malformed client JSON would reach the database unchecked.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Regular expressions (regex): https://en.cppreference.com/w/cpp/regex
 * - JSON schema validation: https://json-schema.org/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Bring in the Validator class and its nested Rule type that we implement here.
#include "validator.hpp"

// For fmt::format, used to build error messages with placeholders.
#include <fmt/core.h>
// Additional fmt formatting support used by the library headers.
#include <fmt/format.h>

// For std::ranges::find_if, used to search the schema columns.
#include <algorithm>
// For std::exception, the base type caught in the catch blocks.
#include <exception>
// For jsoncons::json and its object_range/is_* helpers.
#include <jsoncons/basic_json.hpp>
// jsoncons printing support pulled in by the JSON layer.
#include <jsoncons/pretty_print.hpp>
// For std::optional, holding a string that might not exist.
#include <optional>
// For std::regex and std::regex_match used in field validation.
#include <regex>
#include <string>  // For std::string operations on keys and values.
// For the static maps of patterns and compiled regexes.
#include <unordered_map>
#include <unordered_set>  // For the sets of column names and duplicate keys.
#include <utility>  // For std::pair, the (key, value) item stored in db_data.

// For api::v2::Http::Status/Error used in every failure path.
#include "utils/global/http.hpp"
// For api::v2::ColumnInfo describing each column.
#include "utils/global/types.hpp"
// For DatabaseSchema::getDatabaseSchema, the cached schema.
#include "validator/databaseschema/databaseschema.hpp"

/**
 * See Validator::validateDatabaseCreateSchema.
 * Processing order: reject null/empty input, resolve the table schema, validate
 * present columns' types and missing non-nullable columns, then enforce per-key
 * rules. The duplicated-key check is intentionally disabled because jsoncons
 * parsing already rejects duplicate keys upstream.
 */
bool Validator::validateDatabaseCreateSchema(const std::string&    tablename,
                                             const jsoncons::json& data,
                                             api::v2::Http::Error& error,
                                             const Rule&           rule)
{
  try  // Guard the whole body because JSON access can throw on a badly shaped
       // body.
  {
    if (nullCheck(data,
                  error))  // Stop early when the client sent null or an empty
                           // object.
    {
      return false;  // nullCheck already filled the error, so just report
                     // failure.
    }
    // [ ] I found this is never called as json parser already checks for
    // duplicate keys if (hasDuplicateKeys(data, error))
    // {
    //     error.message = fmt::format("Duplicate key found: {}",
    //     error.message); return false;
    // }

    // Get the schema for the table
    bool found = false;  // Set by the lookup helper to say whether the
                         // table's schema exists.
    auto table_schema =
        getDatabaseSchemaForTable(tablename,
                                  error,
                                  found);  // Fetch the cached columns and
                                           // update found/error.

    if (!found)  // The table has no usable cached schema, so there is
                 // nothing to validate against.
    {
      return false;  // getDatabaseSchemaForTable already set the error;
                     // abort.
    }

    // now lets go through the data and check if the keys are in the schema
    // check for type and nullable

    if (!checkColumns(data,
                      table_schema,
                      error,
                      rule))  // Check every column's type and required-ness.
    {
      return false;  // A column failed, so stop before doing more work.
    }

    // Ensure all keys in the schema are present in the data
    if (!ensureAllKeysExist(data,
                            table_schema,
                            error,
                            rule))  // Check every supplied key is allowed and
                                    // known.
    {
      return false;  // A key was prohibited or unknown; error is already
                     // filled.
    }
    return true;  // All checks passed, so tell the caller the body is
                  // valid.
  }
  catch (const std::exception& e)  // JSON helpers can throw (e.g. "Not an
                                   // object" for a non-object body).
  {
    // Everything in the try block above only ever touches the
    // client-supplied `data` and the static table schema (no DB/network
    // I/O), so any exception reaching here (jsoncons throwing "Not an
    // object" for a non-object body, e.g.) is a malformed-input problem on
    // the client's side, not a server fault - report it as 400, not 500.
    error.message = e.what();  // Copy the exception text so the client
                               // learns what was wrong.
    error.code = api::v2::Http::Status::BAD_REQUEST;  // It is a client mistake,
                                                      // so use HTTP 400.
    return false;  // Report failure to the caller.
  }

  // Unreachable: every path in the try block and the catch returns; kept
  // defensively.
  return true;  // Satisfies the compiler's need for a return after the
                // try/catch.
}

/**
 * See Validator::validateDatabaseUpdateSchema.
 * Unlike the create path, only keys actually present in the body are validated
 * for type; there is no requirement that every schema column appear (partial
 * updates are legal). Immutable/prohibited keys are still rejected through the
 * rule in ensureAllKeysExist.
 */
bool Validator::validateDatabaseUpdateSchema(const std::string&    tablename,
                                             const jsoncons::json& data,
                                             api::v2::Http::Error& error,
                                             const Rule&           rule)
{
  try  // JSON access may throw, so wrap the body just like the create path.
  {
    if (nullCheck(data,
                  error))  // Reject a null or empty body before doing anything
                           // else.
    {
      return false;  // nullCheck already set the error message and code.
    }
    // [ ] I found this is never called as json parser already checks for
    // if (hasDuplicateKeys(data, error))
    // {
    //     error.message = fmt::format("Duplicate key found: {}",
    //     error.message); return false;
    // }

    // Get the schema for the table
    bool found = false;  // Whether the cached schema for this table was found.
    auto table_schema =
        getDatabaseSchemaForTable(tablename,
                                  error,
                                  found);  // Look up the cached columns.

    if (!found)  // No schema means we cannot tell which keys are legal.
    {
      return false;  // The helper filled the error; stop here.
    }
    // Ensure all keys in the schema are present in the data
    // in case of update we dont need to check for all keys
    if (!ensureAllKeysExist(data,
                            table_schema,
                            error,
                            rule))  // Only check that supplied keys are
                                    // allowed; missing ones are fine.
    {
      return false;  // A key was invalid, so abort with the error already
                     // set.
    }
    return true;  // The partial update body passed every check.
  }
  catch (const std::exception& e)  // Catch any JSON exception the same way as
                                   // the create validator.
  {
    // Same reasoning as validateDatabaseCreateSchema's catch above: nothing
    // in this try block can fail for a genuine server-side reason, only for
    // malformed client input.
    error.message = e.what();  // Surface the exception message to the client.
    error.code    = api::v2::Http::Status::BAD_REQUEST;  // Treat it as a 400
                                                         // client error.
    return false;                                        // Report failure.
  }

  // Unreachable: every path in the try block and the catch returns; kept
  // defensively.
  return true;  // Keep the compiler happy after the try/catch.
}

/**
 * See Validator::validateDatabaseReadSchema.
 * Iterates the requested keys once, first applying ASSERT_NOT_PRESENT then
 * checking membership in the cached schema. A key that is both prohibited and
 * unknown is reported as prohibited; a missing table is reported as 404 rather
 * than 400.
 */
bool Validator::validateDatabaseReadSchema(
    const std::unordered_set<std::string>& keys,
    const std::string&                     table_name,
    api::v2::Http::Error&                  error,
    const Rule&                            rule)
{
  bool found =
      false;  // Set by the schema lookup to indicate success or failure.

  // Retrieve the schema for the given table
  auto columns = getDatabaseSchemaForTable(table_name,
                                           error,
                                           found);  // Get the cached columns
                                                    // for this table.
  if (!found)  // The table has no cached schema, so requested columns cannot
               // be checked.
  {
    error.message =
        "Table not found";  // Tell the caller which problem occurred.
    error.code =
        api::v2::Http::Status::NOT_FOUND;  // A missing table is a 404 here
                                           // (unlike the create path).
    return false;                          // Stop with the error filled in.
  }

  // Check if all keys exist in the schema
  for (const auto& key :
       keys)  // Walk each column name the client asked to read.
  {
    if (rule.check(Rule::Action::ASSERT_NOT_PRESENT,
                   key))  // The rule forbids this key from being requested.
    {
      error.message =
          "Key not allowed: " + key;  // Name the prohibited key in the message.
      error.code = api::v2::Http::Status::BAD_REQUEST;  // A forbidden key is a
                                                        // bad request.
      return false;  // Stop at the first prohibited key.
    }
    // Use std::ranges::find_if to check if a column with the given name
    // exists
    auto col = std::ranges::find_if(columns,
                                    [&](const api::v2::ColumnInfo& column)
                                    {
                                      return column.Name == key;
                                    });  // Find a column whose name matches the
                                         // requested key.

    if (col == columns.end())  // No matching column was found in the schema.
    {
      error.message =
          "Key not found in schema: " + key;  // Report the unknown key.
      error.code = api::v2::Http::Status::BAD_REQUEST;  // Asking for a
                                                        // non-existent column
                                                        // is a bad request.
      return false;  // Stop at the first unknown key.
    }
  }

  return true;  // Every requested key was allowed and present in the schema.
}
/**
 * See Validator::clientRegexValidation.
 * Fields with a registered regex must be non-empty strings matching that
 * pattern; unrecognised fields are accepted only if they are non-empty strings.
 * The first offending field stops processing and fills @p error. Accepted pairs
 * are inserted into @p db_data.
 */
bool Validator::clientRegexValidation(
    const jsoncons::json&                                    data,
    api::v2::Http::Error&                                    error,
    std::unordered_set<std::pair<std::string, std::string>>& db_data)
{
  static const std::unordered_map<std::string, std::string>
      regex_client_validators = {
          // Map field names to regex text; static so it is built only once.
          {"username",
           "^[a-z][a-z0-9_]*$"},  // Starts with a lowercase letter, then
                                  // letters/digits/underscores.
          {"password",
           "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[!@#$%^&*])[A-Za-z\\d!@#$"
           "%^&*"
           "]{"
           "8,}$"},  // At least 8 chars with a lowercase, uppercase, digit
          // and special character.
          {"phone",
           // Optional country code and separators around a phone number.
           R"(^\+?(\d{1,3})?[-.\s]?(\(?\d{3}\)?)?[-.\s]?\d{3}[-.\s]?\d{4}$)"},
          // Something@something.something style address.
          {"email", R"((\w+)(\.\w+)*@(\w+)(\.\w+)+)"},
          {"dob",
           // Date in DD-MM-YYYY form.
           R"(^(0[1-9]|[12]\d|3[01])-(0[1-9]|1[0-2])-\d{4}$)"},
          {"gender", "^(Male|Female)$"},  // Exactly "Male" or "Female".
  };

  // Compiling a std::regex is expensive, so it happens once, not for every
  // request and field.
  static const std::unordered_map<std::string, std::regex> compiled_validators =
      []  // Immediate lambda builds the compiled map once at first use.
  {
    std::unordered_map<std::string, std::regex>
        compiled;  // Empty map that will hold field name -> compiled regex.
    for (const auto& [name, pattern] :
         regex_client_validators)  // Visit every (field name, pattern text)
                                   // pair above.
    {
      compiled.emplace(name,
                       std::regex(
                           pattern));  // Compile the text into a regex and
                                       // store it under the field name.
    }
    return compiled;  // Hand the finished map back to initialise
                      // compiled_validators.
  }();

  for (const auto& item : data.object_range())  // Walk every key/value pair in
                                                // the client's JSON object.
  {
    auto pattern_item = compiled_validators.find(std::string(
        item.key()));  // Look for a regex registered for this field name.

    // Keys without a dedicated regex are only kept when the value is a
    // non-empty string; other JSON types are silently dropped here rather
    // than passed through.
    if (pattern_item ==
        compiled_validators.end())  // No regex rule exists for this field name.
    {
      if (item.value().is_string())  // Only plain strings are accepted
                                     // for unvalidated fields.
      {
        std::optional<std::string> v =
            item.value().as<std::string>();  // Try to read the value as
                                             // a string.
        if (v.has_value() && !v->empty())  // Keep it only if conversion worked
                                           // and the text is not empty.
        {
          db_data.insert(
              {std::string(item.key()), *v});  // Store the (key, value) pair
                                               // for the database call.
        }
      }
      continue;  // Move on to the next field; no regex check applies
                 // here.
    }

    if (!item.value().is_string())  // A field with a regex must be a
                                    // string, so reject other types.
    {
      error.message =
          "Value must be a string for key " +
          std::string(item.key());  // Say which field had the wrong type.
      // Wrong type is a client error.
      error.code = api::v2::Http::Status::BAD_REQUEST;
      return false;  // Stop at the first invalid field.
    }
    std::optional<std::string> value =
        item.value().as<std::string>();  // Read the string value now that
                                         // the type is known.
    if (value.has_value() &&
        !value->empty())  // Only non-empty strings can be pattern matched.
    {
      if (!std::regex_match(value.value(),
                            pattern_item
                                ->second))  // The whole value must match the
                                            // compiled pattern.
      {
        error =
            {.code = api::v2::Http::Status::BAD_REQUEST,
             .message =
                 fmt::format("Key ({}) value is invalid.",
                             item.key())};  // Fill code and message in one go.
        return false;  // Stop at the first field that fails its
                       // pattern.
      }
      db_data.insert(
          {item.key(), value.value()});  // The value passed, so store it
                                         // for the database call.
    }
    else  // The value is missing or an empty string.
    {
      error.message = "Value is empty for key " +
                      item.key();  // Tell the caller which field was empty.
      error.code = api::v2::Http::Status::BAD_REQUEST;  // Empty required value
                                                        // is a client error.
      return false;  // Stop at the first empty field.
    }
  }
  return true;  // Every field passed its check (or was safely ignored).
}

// Add this method declaration in the private section
/**
 * See Validator::hasDuplicateKeys.
 * Uses a temporary set of key names; the first repeated name fills @p error and
 * returns true. Kept for defensive use even though the JSON parser rejects
 * duplicates first.
 */
bool Validator::hasDuplicateKeys(const jsoncons::json& data,
                                 api::v2::Http::Error& error)
{
  std::unordered_set<std::string> keys;  // Remember key names we have already
                                         // seen; a set keeps them unique.

  for (const auto& item :
       data.object_range())  // Look at each key/value pair in the object.
  {
    if (!keys.insert(item.key())
             .second)  // insert returns second=false when the key was
                       // already present (a duplicate).
    {
      error.message = item.key();  // Report the name of the duplicated key.
      error.code = api::v2::Http::Status::BAD_REQUEST;  // Duplicate keys are a
                                                        // client mistake.
      return true;  // Tell the caller a duplicate was found.
    }
  }
  return false;  // No key repeated, so the object is fine.
}

/**
 * See Validator::nullCheck.
 */
bool Validator::nullCheck(const jsoncons::json& data,
                          api::v2::Http::Error& error)
{
  if (data.is_null() ||
      data.empty())  // Reject both a JSON null and an empty object/array.
  {
    error.message = "Data is empty";  // Explain that no usable data was sent.
    error.code    = api::v2::Http::Status::BAD_REQUEST;  // Empty input is a
                                                         // client error.
    return true;  // True means "stop validation now".
  }
  return false;  // There is data, so validation can continue.
}

/**
 * See Validator::getDatabaseSchemaForTable.
 * Reads the process-wide schema snapshot via
 * DatabaseSchema::getDatabaseSchema(). An absent table or an entry with no
 * columns is treated as "not found" and sets @p found to false; the returned
 * set is a copy so callers cannot mutate the cached schema.
 */
std::unordered_set<api::v2::ColumnInfo> Validator::getDatabaseSchemaForTable(
    const std::string& tablename, api::v2::Http::Error& error, bool& found)
{
  SCHEMA_t schema =
      DatabaseSchema::getDatabaseSchema();  // Get a copy of the whole cached
                                            // schema (table -> columns).

  // Ensure the table schema exists
  auto table_schema_it =
      schema.find(tablename);  // Look for this table's entry in the schema map.
  if (table_schema_it == schema.end() ||
      table_schema_it->second
          .empty())  // Missing entry or an entry with no columns is unusable.
  {
    error.message = "Table not found";  // Report that we cannot validate
                                        // against this table.
    error.code = api::v2::Http::Status::BAD_REQUEST;  // Treat the unknown table
                                                      // as a bad request here.
    found      = false;  // Tell the caller the lookup failed.
    return {};  // Return an empty set so the caller has nothing to iterate.
  }
  found = true;  // The schema was found and has columns.
  return table_schema_it
      ->second;  // Hand back a copy of this table's column set.
}
/**
 * See Validator::validateType.
 * "character varying" and all date/time spellings expect JSON strings; "jsonb"
 * accepts either an object or an array. Date strings are not parsed here, only
 * type-checked. Unknown schema type strings return false, which surfaces as a
 * type mismatch to the caller.
 */
bool Validator::validateType(const jsoncons::json& value,
                             const std::string&    expectedType)
{
  if (expectedType == "integer")  // The schema expects a whole number.
  {
    return value
        .is_int64();  // True only when the JSON value is a 64-bit integer.
  }
  if (expectedType == "float" || expectedType == "double" ||
      expectedType == "real")  // All three mean a decimal number in PostgreSQL.
  {
    return value.is_double();  // True only when the JSON value is a
                               // floating-point number.
  }
  if (expectedType ==
      "character varying")  // PostgreSQL's name for variable-length text.
  {
    return value.is_string();  // True only when the JSON value is a string.
  }
  if (expectedType == "boolean")  // A true/false column.
  {
    return value.is_bool();  // True only when the JSON value is a boolean.
  }
  if (expectedType == "jsonb")  // A column that stores JSON itself.
  {
    return (value.is_object() ||
            value.is_array());  // Accept either a JSON object or a JSON array.
  }
  if (expectedType == "timestamp with time zone" ||
      expectedType == "time without time zone" ||
      expectedType == "date")  // All date/time spellings.
  {
    return value.is_string();  // Assuming dates are strings
  }
  return false;  // Unknown type
}

/**
 * See Validator::checkColumns.
 * Iterates schema columns (the authoritative source), not the body. A column
 * that is absent is allowed when it is nullable or covered by
 * IGNORE_IF_NOT_NULLABLE_IN_SCHEMA; a present column must match the schema's
 * type according to validateType. The first violation aborts with HTTP 400.
 */
bool Validator::checkColumns(
    const jsoncons::json&                          data,
    const std::unordered_set<api::v2::ColumnInfo>& table_schema,
    api::v2::Http::Error&                          error,
    const Rule&                                    rule)
{
  for (const auto& column : table_schema)  // Walk the schema's columns, which
                                           // are the source of truth.
  {
    bool column_exists = data.contains(
        column.Name);  // Whether the client supplied this column at all.

    // Check for missing non-nullable columns
    if (!column_exists && !column.isNullable &&
        !rule.check(Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA,
                    column.Name))  // Missing, required, and not excused by the
                                   // rule.
    {
      error.message = "Non-nullable column missing in data: " +
                      column.Name;  // Name the missing required column.
      error.code =
          api::v2::Http::Status::BAD_REQUEST;  // A missing required field
                                               // is a client error.
      return false;  // Stop at the first missing required column.
    }

    // Validate data type if column exists
    if (column_exists)  // Only check the type when the client actually sent
                        // this column.
    {
      const auto& value = data.at(column.Name);  // Fetch the value for this
                                                 // column from the JSON body.
      if (!validateType(value,
                        column.DataType))  // Compare the value's JSON kind to
                                           // the schema's declared type.
      {
        error.message =
            fmt::format("Data type mismatch for column: {} ,expected: {} ",
                        column.Name,
                        column.DataType);  // Build a helpful mismatch message.
        error.code = api::v2::Http::Status::BAD_REQUEST;  // A wrong type is a
                                                          // client error.
        return false;  // Stop at the first type mismatch.
      }
    }
  }
  return true;  // Every column constraint was satisfied.
}
/**
 * See Validator::ensureAllKeysExist.
 * Builds a name set from the schema for O(1) membership tests, then scans the
 * body. ASSERT_NOT_PRESENT and ASSERT_IMMUTABLE are checked first so
 * prohibited/immutable keys are reported specifically; unknown keys are allowed
 * only under IGNORE_IF_MISSING_FROM_SCHEMA.
 */
bool Validator::ensureAllKeysExist(
    const jsoncons::json&                          data,
    const std::unordered_set<api::v2::ColumnInfo>& table_schema,
    api::v2::Http::Error&                          error,
    const Rule&                                    rule)
{
  // Create a set of column names for fast key lookup
  std::unordered_set<std::string>
      schema_columns;  // Will hold just the names, for quick membership
                       // checks.
  for (const auto& column :
       table_schema)  // Walk the schema's column descriptions once.
  {
    schema_columns.insert(
        column.Name);  // Add each column name to the fast-lookup set.
  }

  // Ensure all keys in `data` exist in the schema
  for (const auto& entry :
       data.object_range())  // Visit every key the client actually supplied.
  {
    const auto& key = entry.key();  // The current field name being checked.

    // Check for missing not allowed keys
    if (rule.check(Rule::Action::ASSERT_NOT_PRESENT,
                   key))  // The rule says this key must never appear.
    {
      error.message = "A prohibited key was found: [" + key +
                      "]";  // Name the forbidden key.
      // Including it is a client error.
      error.code = api::v2::Http::Status::BAD_REQUEST;
      return false;  // Stop at the first prohibited key.
    }
    // check if the key is in the exclude list
    if (rule.check(Rule::Action::ASSERT_IMMUTABLE,
                   key))  // The rule says this key cannot be changed.
    {
      error.message = "Key: [" + key +
                      "] is not allowed to be changed";  // Explain that the key
                                                         // is immutable.
      error.code = api::v2::Http::Status::BAD_REQUEST;   // Trying to change it
                                                         // is a client error.
      return false;  // Stop at the first immutable key.
    }

    bool ignore_if_missing_from_database_schema =
        rule.check(Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA,
                   key);  // Whether the rule excuses keys absent from the
                          // schema.
    if (schema_columns.find(key) == schema_columns.end() &&
        !ignore_if_missing_from_database_schema)  // Key not in schema
    {
      error.message =
          "Key: [" + key +
          "] is not found in database schema";  // Report the unknown key.
      error.code = api::v2::Http::Status::BAD_REQUEST;  // An unknown key is a
                                                        // client error.
      return false;  // Stop at the first unknown key.
    }
  }
  return true;  // All supplied keys were permitted and known to the schema.
}
