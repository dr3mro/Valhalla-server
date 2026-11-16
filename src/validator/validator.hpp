/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file validator.hpp
 * @brief Stateless collection of checks applied to client JSON before it
 * reaches the database.
 * @details This header declares the Validator class: a toolbox of static
 * functions that check JSON sent by a client against the shape of a database
 * table. It also declares the nested Rule type, which lets a caller describe
 * exceptions to the usual checks. None of these functions talks to the database
 * directly; the checks run on an in-memory copy of the schema, so they are
 * cheap enough to run on every incoming request.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the gatekeeper between the HTTP layer and the database. Entity
 * controllers (for clients, staff, appointments and so on) call these helpers
 * with the parsed request body before they build any SQL. The helpers read the
 * cached schema owned by DatabaseSchema. Without this file, badly shaped or
 * mistyped client data would flow straight into SQL queries and cause errors or
 * security problems, so every create, update and read endpoint depends on it.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Bit flags / bitmasks:
 * https://www.learncpp.com/cpp-tutorial/bitwise-operators/
 * - Regular expressions (regex): https://en.cppreference.com/w/cpp/regex
 * - JSON schema validation: https://json-schema.org/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Compile this header at most once per source file, no matter how
              // often it is included.

// For std::ranges::find_if, used to search containers for a matching item.
#include <algorithm>
// For std::uint8_t, the small integer type behind the Action flags.
#include <cstdint>
// For jsoncons::json, the JSON value type that we validate.
#include <jsoncons/basic_json.hpp>
#include <string>  // For std::string, used for column/key names and type names.
// For std::unordered_map, mapping action flags to sets of keys.
#include <unordered_map>
// For std::unordered_set, holding unique keys and columns.
#include <unordered_set>
// For std::pair, the (key, value) item type stored in db_data.
#include <utility>

// For api::v2::Http::Error and Status, used to report failures.
#include "utils/global/http.hpp"
// For api::v2::ColumnInfo, the column-description type.
#include "utils/global/types.hpp"
/**
 * @class Validator
 * @brief Stateless collection of checks applied to client JSON before it
 * reaches the database.
 *
 * @details Entity controllers call these helpers with the parsed request body,
 * the target table name and a Rule describing per-key exceptions. Validation is
 * performed against a cached snapshot of the database schema (DatabaseSchema),
 * not against a live query, so the helpers are cheap enough for the request
 * path. Every entry point is static and reports failures by filling an
 * api::v2::Http::Error (code + message) and returning false rather than
 * throwing; exceptions from the JSON layer are caught and converted to 400
 * responses.
 *
 * @note All members are static; Validator carries no state and needs no
 * lifetime management.
 * @warning The cached schema must have been populated before these helpers are
 * used, otherwise table lookups fail and the validators return false.
 * @see Rule, DatabaseSchema, api::v2::Http::Error
 */
class Validator
{
 public:
  /** @brief Default constructor; the class is stateless. */
  Validator() = default;
  /** @brief Virtual destructor. */
  virtual ~Validator() = default;
  /** @brief Copy constructor; no state to copy. */
  Validator(const Validator& other) = default;
  /** @brief Copy assignment; no state to assign. */
  Validator& operator=(const Validator& other) = default;
  /** @brief Move constructor; no state to move. */
  Validator(Validator&& other) noexcept = default;
  /** @brief Move assignment; no state to move. */
  Validator& operator=(Validator&& other) noexcept = default;

  /**
   * @struct Rule
   * @brief A set of validation exceptions keyed by the actions they relax or
   * assert.
   *
   * @details Holds one or more bitmask Action flags and, for each flag group,
   * the set of column names it applies to. A check passes only when the
   * relevant flag is present in
   * @ref action and a matching flag group contains the key being checked.
   * Rules are typically static const objects in controller code, composed
   * with the bitwise operators.
   * @note Copying/moving is implicit; the key sets own their strings.
   */
  struct Rule
  {
    /**
     * @brief Bitmask of validation relaxations/assertions that make up a
     * rule.
     * @details Values are powers of two and may be combined with operator|.
     */
    enum Action : std::uint8_t
    {
      NONE = 0,  // No flags set: the "do nothing" value used when testing
                 // flags.
      IGNORE_IF_MISSING_FROM_SCHEMA = 1U
          << 0U,  // Ignore if key is missing from database schema
      IGNORE_IF_NOT_NULLABLE_IN_SCHEMA = 1U
          << 1U,  // Ignore if key is not nullable in database schema
      ASSERT_IMMUTABLE   = 1U << 2U,  // Assert that key is immutable
      ASSERT_NOT_PRESENT = 1U << 3U,  // Assert that key is not present in json
    };

    /** @brief Combined action flags that this rule activates. */
    Action action;  // NOLINT - the on/off flags, kept public so callers can
                    // compose rules.
    /** @brief Maps an action flag (or group of flags) to the keys it
     * applies to. */
    std::unordered_map<Action, std::unordered_set<std::string>>
        keys;  // NOLINT - each flag group owns the keys it relaxes.

    /**
     * @brief Tests whether a given action applies to a given key.
     * @param[in] check Action flag being tested.
     * @param[in] key Column/key name being validated.
     * @return True only when @p check is set in this rule and @p key
     * belongs to a flag group that intersects @p check.
     */
    // NOLINT
    bool check(const Action& check, const std::string& key) const
    {
      bool isMatch = hasMatch(check);  // True when this rule's flags
                                       // include the flag we are testing.
      bool isKey =
          hasKey(check,
                 key);  // True when the key sits in a matching flag group.
      return isMatch && isKey;  // Both must hold, so return their logical
                                // AND to the caller.
    }

    // constructor for rule
    /**
     * @brief Constructs a rule combining @p _action with the given key set.
     * @param[in] _action Action flags (usually a bitwise OR of several).
     * @param[in] _keys Keys the flags apply to; taken by value into @ref
     * keys.
     * @pre At least one Action flag should be set, otherwise the rule can
     * never match.
     */
    explicit Rule(Action _action, const std::unordered_set<std::string>&& _keys)
        : action(_action)
    {
      this->keys[action] = _keys;
    }

    /** @brief Rules require an explicit action and key set. */
    Rule() = delete;

    // Overload | operator
    /** @brief Combines two action flags into one bitmask. */
    friend Action operator|(Action first, Action second)
    {
      return static_cast<Action>(
          static_cast<std::uint8_t>(first) |
          static_cast<std::uint8_t>(second));  // OR the raw bytes, then treat
                                               // the result as an Action again.
    }

    // Overload & operator
    /** @brief Returns the intersection of two action bitmasks. */
    friend Action operator&(Action first, Action second)
    {
      return static_cast<Action>(
          static_cast<std::uint8_t>(first) &
          static_cast<std::uint8_t>(
              second));  // AND the raw bytes to keep only shared flags.
    }

    // Overload |= operator
    /** @brief Adds @p second's flags to @p first in place. */
    friend Action& operator|=(Action& first, Action second)
    {
      first = first | second;  // Reuse operator| and store the combined
                               // flags back into first.
      return first;  // Return the updated object so calls can be chained.
    }

    // Overload &= operator
    /** @brief Keeps only the flags common to @p first and @p second. */
    friend Action& operator&=(Action& first, Action second)
    {
      first = first & second;  // Reuse operator& and store the narrowed
                               // flags back into first.
      return first;  // Return the updated object so calls can be chained.
    }

    // Invert operator
    /** @brief Inverts all eight bits; include NONE filtering at the call
     * site. */
    friend Action operator~(Action first)
    {
      return static_cast<Action>(~static_cast<std::uint8_t>(first));
    }

    // Check if any flag is set
    /** @brief Convenience conversion: true when at least one action flag is
     * set. */
    explicit operator bool() const
    {
      return static_cast<int>(action) != 0;
    }

   private:
    // check if rule is set
    /** @brief True when the rule's combined action intersects @p check. */
    bool hasMatch(const Action& check) const
    {
      return (action & check) != NONE;
    }  // NOLINT
    /** @brief True when a stored flag group intersects @p check. */
    bool hasMatch(const Action& key, const Action& check) const
    {
      return (key & check) != NONE;
    }  // NOLINT

    // check if key exists in rule for and action
    /** @brief True when @p element appears in a key set whose flags
     * intersect @p check. */
    bool hasKey(
        // NOLINT
        const Action&      check,
        const std::string& element) const
    {
      return std::ranges::find_if(this->keys,
                                  [&](const auto& key)
                                  {
                                    return (hasMatch(key.first, check)) &&
                                           (key.second.contains(element));
                                  }) !=
             this->keys.end();  // Found a matching group means the key is
                                // allowed; reaching end() means it is not.
    }
  };

  /**
   * @brief Validates a create (INSERT) request body against a table's schema.
   * @param[in] tablename Target table used for the schema lookup.
   * @param[in] data Parsed JSON body; must be a non-empty object.
   * @param[out] error Filled with code/message on failure; HTTP 400 for
   * malformed input or missing/non-nullable columns, HTTP 400 for an unknown
   * table.
   * @param[in] rule Per-key exceptions (e.g. ignore a server-generated "id").
   * @return True when the body satisfies all schema constraints.
   * @details Runs null/empty check, schema lookup, per-column type and
   * nullability checks (checkColumns), and key-presence/prohibition checks
   * (ensureAllKeysExist). Exceptions from jsoncons (such as a non-object
   * body) are caught and reported as BAD_REQUEST, since these inputs are
   * client errors rather than server faults.
   * @see validateDatabaseUpdateSchema, checkColumns, ensureAllKeysExist
   */
  static bool validateDatabaseCreateSchema(const std::string&    tablename,
                                           const jsoncons::json& data,
                                           api::v2::Http::Error& error,
                                           const Rule&           rule);
  /**
   * @brief Validates an update (PATCH/PUT) request body against a table's
   * schema.
   * @param[in] tablename Target table used for the schema lookup.
   * @param[in] data Parsed JSON body; only a subset of columns needs to be
   * present.
   * @param[out] error Filled with code/message on failure (HTTP 400).
   * @param[in] rule Per-key exceptions, notably ASSERT_IMMUTABLE for
   * read-only columns.
   * @return True when the (partial) body is valid for an update.
   * @details Checks null/empty input and key-level constraints; unlike the
   * create path it does not require every schema column to be present.
   * Exceptions are translated to 400 for the same reasons as in
   * validateDatabaseCreateSchema.
   */
  static bool validateDatabaseUpdateSchema(const std::string&    tablename,
                                           const jsoncons::json& data,
                                           api::v2::Http::Error& error,
                                           const Rule&           rule);
  /**
   * @brief Validates the projected columns of a read (SELECT) request.
   * @param[in] keys Column names requested by the client.
   * @param[in] table_name Table the columns are requested from.
   * @param[out] error Filled with code/message on failure: 404 if the table
   * has no schema, 400 if a key is prohibited by the rule or absent from the
   * schema.
   * @param[in] rule Per-key exceptions, notably ASSERT_NOT_PRESENT for
   * forbidden columns.
   * @return True when every requested key exists in the schema and none is
   * prohibited.
   */
  static bool validateDatabaseReadSchema(
      const std::unordered_set<std::string>& keys,
      const std::string&                     table_name,
      api::v2::Http::Error&                  error,
      const Rule&                            rule);
  /**
   * @brief Applies field-specific regex rules to client-supplied entity data.
   * @param[in] data JSON object whose fields are validated by name (username,
   * password, phone, email, dob, gender). Fields without a rule are passed
   * through if they are non-empty strings.
   * @param[out] error Filled with 400 and a descriptive message on the first
   * bad field.
   * @param[out] db_data Receives the validated (key, value) pairs ready for a
   * database call.
   * @return True when all fields pass; false on the first invalid or empty
   * value.
   * @details Regexes are compiled once into a function-local static map
   * because compiling std::regex per request is expensive. A field with a
   * validator must be a string, non-empty and match its pattern; an
   * unvalidated field is copied only when it is a non-empty string.
   * @note Entries already present in @p db_data are preserved; results are
   * inserted, not assigned.
   */
  static bool clientRegexValidation(
      const jsoncons::json&                                    data,
      api::v2::Http::Error&                                    error,
      std::unordered_set<std::pair<std::string, std::string>>& db_data);

 private:
  /**
   * @brief Rejects a null or empty JSON body as a client error.
   * @param[in] data Body to inspect.
   * @param[out] error Set to 400 / "Data is empty" when the check fails.
   * @return True when @p data is null or empty (i.e. validation should stop).
   */
  static bool nullCheck(const jsoncons::json& data,
                        api::v2::Http::Error& error);
  /**
   * @brief Looks up a table's cached column set.
   * @param[in] tablename Table to look up.
   * @param[out] error Set to 400 / "Table not found" when the schema is
   * missing or empty.
   * @param[out] found Set true on success, false on failure.
   * @return A copy of the table's ColumnInfo set, or an empty set when not
   * found.
   * @note The caller must consult @p found; an empty successful schema is
   * treated as failure.
   */
  static std::unordered_set<api::v2::ColumnInfo> getDatabaseSchemaForTable(
      const std::string& tablename, api::v2::Http::Error& error, bool& found);
  /**
   * @brief Maps a PostgreSQL type name to a jsoncons type predicate.
   * @param[in] value JSON value to test.
   * @param[in] expectedType Schema type string (integer, float/double/real,
   * character varying, boolean, jsonb, or date/time variants).
   * @return True when the JSON value's kind matches the schema type; false
   * otherwise or for unrecognised type strings.
   */
  static bool validateType(const jsoncons::json& value,
                           const std::string&    expectedType);
  /**
   * @brief Checks each schema column against the body (create path).
   * @param[in] data JSON object under validation.
   * @param[in] table_schema Cached column definitions for the table.
   * @param[out] error Set to 400 on a missing non-nullable column or a type
   * mismatch.
   * @param[in] rule Relaxations, notably IGNORE_IF_NOT_NULLABLE_IN_SCHEMA.
   * @return True when every column constraint is satisfied.
   */
  static bool checkColumns(
      const jsoncons::json&                          data,
      const std::unordered_set<api::v2::ColumnInfo>& table_schema,
      api::v2::Http::Error&                          error,
      const Rule&                                    rule);
  /**
   * @brief Checks that every key in the body is permitted and known to the
   * schema.
   * @param[in] data JSON object whose keys are checked.
   * @param[in] table_schema Cached column definitions used for the membership
   * test.
   * @param[out] error Set to 400 on a prohibited, immutable or unknown key.
   * @param[in] rule Assertions and relaxations (ASSERT_NOT_PRESENT,
   * ASSERT_IMMUTABLE, IGNORE_IF_MISSING_FROM_SCHEMA).
   * @return True when all keys are acceptable.
   * @note This checks keys in the data, not that all schema columns are
   * present; the latter is enforced by checkColumns on the create path.
   */
  static bool ensureAllKeysExist(
      const jsoncons::json&                          data,
      const std::unordered_set<api::v2::ColumnInfo>& table_schema,
      api::v2::Http::Error&                          error,
      const Rule&                                    rule);
  /**
   * @brief Detects duplicate keys in a JSON object.
   * @param[in] data Object to scan.
   * @param[out] error Set to 400 with the duplicate key name when one is
   * found.
   * @return True when a duplicate key is present.
   * @note Currently unused by the create/update validators: the jsoncons
   * parser already rejects duplicate keys before these helpers run.
   */
  static bool hasDuplicateKeys(const jsoncons::json& data,
                               api::v2::Http::Error& error);
};
