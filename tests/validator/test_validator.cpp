/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_validator.cpp
 * @brief Checks rule flags, input regex validation, search-request parsing and
 * schema validation.
 * @details The tests cover the Rule flag type (including bitwise operators),
 * client field validation such as usernames, emails and passwords, parsing of
 * Search_t requests with strict identifier rules, and safe handling of unknown
 * tables. Several cases are regression guards for bugs where bad input used to
 * crash or where SQL syntax could slip into a query.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the input-validation layer that stands between HTTP requests and the
 * database. If a rule flag were ignored, a forbidden field could be changed; if
 * a regex were too loose, malformed data would be stored; if filter/order names
 * were not plain identifiers, SQL injection would be possible. These tests pin
 * those checks so hostile input is rejected with a 400 instead of harming the
 * server.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases, sections and CAPTURE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertions (CHECK_FALSE, REQUIRE_FALSE):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - SQL injection and why identifiers matter:
 * https://owasp.org/www-community/attacks/SQL_Injection
 * - try/catch (handling parse errors):
 * https://en.cppreference.com/w/cpp/language/try_catch
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Catch2's TEST_CASE, SECTION, CHECK, CAPTURE and REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
#include <cstdint>            // Fixed-width integer types used in test data.
#include <jsoncons/json.hpp>  // The JSON value type validated by these tests.
#include <optional>  // std::optional support for the getters used here.
#include <string>    // std::string for field names and values.
// The set passed to regex validation for collected pairs.
#include <unordered_set>
#include <utility>  // std::pair, used for key/value pairs in the test data.

// Types::Search_t, one of the structures parsed in these tests.
#include "entities/base/types.hpp"
#include "utils/global/http.hpp"    // Http::Status codes such as BAD_REQUEST.
#include "validator/validator.hpp"  // Validator, the class under test.

using Validator = ::Validator;  // Shorthand for the global Validator type.
using HttpError = api::v2::Http::Error;  // Shorthand for the error struct
                                         // filled on validation failure.
namespace Types = api::v2::Types;  // Shorthand for the data-type namespace.

namespace  // Anonymous namespace keeps the helper private to this file.
{
  /** @brief Builds a string-valued JSON object from key/value pairs for the
   * regex-validation cases. */
  jsoncons::json makeJsonObject(
      std::initializer_list<std::pair<const char*, std::string>>
          items)  // Turns pairs into a JSON object.
  {
    jsoncons::json object =
        jsoncons::json::object();           // Start with an empty object.
    for (const auto& [key, value] : items)  // Loop over each pair.
    {
      object[key] = value;  // Store the value under its key.
    }
    return object;  // Return the finished object.
  }
}  // namespace

TEST_CASE("Validator::Rule flag semantics",
          "[validator][rule]")  // How rule flags match keys and combine.
{
  using Action = Validator::Rule::Action;  // Shorthand for the action enum.

  const Validator::Rule
      rule(Action::ASSERT_IMMUTABLE,
           {"id", "created_at"});  // A rule covering two keys for one action.

  SECTION(
      "a key covered by the rule matches")  // Both listed keys should match.
  {
    CHECK(rule.check(Action::ASSERT_IMMUTABLE, "id"));  // "id" is covered.
    CHECK(rule.check(Action::ASSERT_IMMUTABLE,
                     "created_at"));  // "created_at" is covered.
  }

  SECTION("a key not covered does not match")
  {
    CHECK_FALSE(rule.check(Action::ASSERT_IMMUTABLE, "name"));
  }  // "name" is not in the rule's set.

  SECTION(
      // Action must also match.
      "a different action does not match even for covered keys")
  {
    CHECK_FALSE(rule.check(Action::ASSERT_NOT_PRESENT,
                           "id"));  // Same key, different action: no match.
    CHECK_FALSE(rule.check(Action::IGNORE_IF_MISSING_FROM_SCHEMA,
                           "id"));  // And another different action.
  }

  SECTION("explicit operator bool reflects the action")  // Truthiness follows
                                                         // whether an action
                                                         // is set.
  {
    CHECK(static_cast<bool>(rule));  // The rule has an action, so it is truthy.
    const Validator::Rule none(Action::NONE, {"id"});  // A rule with no action.
    CHECK_FALSE(static_cast<bool>(none));              // No action means falsy.
  }

  SECTION("bit operators behave as flags")  // Actions can be combined with
                                            // bitwise operators.
  {
    auto flags = Action::ASSERT_IMMUTABLE;  // Start with one flag.
    flags |= Action::ASSERT_NOT_PRESENT;    // Add a second flag.
    CHECK((flags & Action::ASSERT_IMMUTABLE) ==
          Action::ASSERT_IMMUTABLE);  // First flag is still present.
    CHECK((flags & Action::ASSERT_NOT_PRESENT) ==
          Action::ASSERT_NOT_PRESENT);  // Second flag is present.
    CHECK((flags & Action::IGNORE_IF_MISSING_FROM_SCHEMA) ==
          Action::NONE);  // Unset flag reads as NONE.
    CHECK((~Action::NONE) !=
          Action::NONE);  // Inverting NONE produces all flags.
  }
}

TEST_CASE("Validator::Rule keeps key sets per action",
          "[validator][rule]")  // Keys are stored per action.
{
  using Action = Validator::Rule::Action;  // Shorthand for the action enum.

  // The keys map is keyed by action, so a rule can cover several actions.
  Validator::Rule rule(Action::ASSERT_IMMUTABLE,
                       {"id"});  // One action with one key.

  // The public constructor stores the set under the rule's action.
  CHECK(rule.check(Action::ASSERT_IMMUTABLE,
                   "id"));  // The key matches for that action.
  CHECK_FALSE(rule.check(Action::ASSERT_IMMUTABLE,
                         "other"));  // A different key does not.
}

TEST_CASE("Validator::clientRegexValidation accepts known-good client data",
          "[validator][regex]")  // Valid fields pass and are collected.
{
  jsoncons::json data = makeJsonObject({
      // A well-formed client record.
      {"username", "john_doe"},
      {"email", "john.doe@example.com"},
      {"dob", "31-12-1990"},
      {"gender", "Male"},
  });

  HttpError error;  // Receives any validation error.
  std::unordered_set<std::pair<std::string, std::string>>
      db_data;  // Receives the validated key/value pairs.
  REQUIRE(
      Validator::clientRegexValidation(data,
                                       error,
                                       db_data));  // Validation must succeed.

  // Every validated pair is passed through to db_data.
  CHECK(db_data.count({"username", "john_doe"}) ==
        1);  // Username was collected.
  CHECK(db_data.count({"email", "john.doe@example.com"}) ==
        1);  // Email was collected.
}

TEST_CASE(
    "Validator::clientRegexValidation rejects values that do not match their "
    "pattern",
    "[validator][regex]")  // Bad fields are rejected.
{
  const std::vector<std::pair<std::string, std::string>> invalid = {
      // A list of invalid field/value pairs.
      {"username", "1starts_with_digit"},
      {"username", "Has Uppercase"},
      {"email", "not-an-email"},
      {"dob", "1990-12-31"},  // wrong separator order (expects dd-mm-yyyy)
      {"gender", "male"},     // case sensitive
  };

  for (const auto& [key, value] : invalid)  // Try each invalid pair in turn.
  {
    CAPTURE(key,
            value);  // Show the failing pair in the test output if it fails.

    jsoncons::json data =
        makeJsonObject({{key.c_str(), value}});  // Build a one-field payload.

    HttpError error;  // Receives the validation error.
    std::unordered_set<std::pair<std::string, std::string>>
        db_data;  // Receives validated pairs (should stay empty).
    REQUIRE_FALSE(
        Validator::clientRegexValidation(data,
                                         error,
                                         db_data));  // Validation must fail.
    CHECK(error.code ==
          api::v2::Http::Status::BAD_REQUEST);  // Failure is reported as 400.
    CHECK_FALSE(error.message.empty());  // An explanatory message is provided.
  }
}

TEST_CASE("Validator::clientRegexValidation password policy",
          "[validator][regex]")  // Password strength rules.
{
  const std::vector<std::string> valid = {
      // Passwords that meet the policy.
      "Abcdef1!",  // lower, upper, digit, special, 8 chars
      "Longer1Password!",
  };
  const std::vector<std::string> invalid = {
      // Passwords that break one rule each.
      "abcdef1!",   // no uppercase
      "ABCDEF1!",   // no lowercase
      "Abcdefgh!",  // no digit
      "Abcdef12",   // no special character
      "Ab1!",       // shorter than 8
  };

  for (const auto& password : valid)  // Check the accepted passwords.
  {
    CAPTURE(password);  // Print the password if a check fails.
    jsoncons::json data =
        makeJsonObject({{"password", password}});  // One-field payload.
    HttpError error;                               // Receives any error.
    std::unordered_set<std::pair<std::string, std::string>>
        db_data;  // Receives validated pairs.
    CHECK(Validator::clientRegexValidation(data,
                                           error,
                                           db_data));  // Must be accepted.
  }

  for (const auto& password : invalid)  // Check the rejected passwords.
  {
    CAPTURE(password);  // Print the password if a check fails.
    jsoncons::json data =
        makeJsonObject({{"password", password}});  // One-field payload.
    HttpError error;                               // Receives the error.
    std::unordered_set<std::pair<std::string, std::string>>
        db_data;  // Receives validated pairs.
    CHECK_FALSE(
        Validator::clientRegexValidation(data,
                                         error,
                                         db_data));  // Must be rejected.
    CHECK(error.code ==
          api::v2::Http::Status::BAD_REQUEST);  // Reported as 400.
  }
}

TEST_CASE("Validator::clientRegexValidation rejects empty values",
          "[validator][regex]")  // Empty field values fail.
{
  jsoncons::json data =
      makeJsonObject({{"username", ""}});  // An empty username.

  HttpError error;  // Receives the error.
  std::unordered_set<std::pair<std::string, std::string>>
      db_data;  // Receives validated pairs.
  REQUIRE_FALSE(
      Validator::clientRegexValidation(data,
                                       error,
                                       db_data));  // Empty value is rejected.
  CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);  // Reported as 400.
  CHECK(error.message.find("username") !=
        std::string::npos);  // The message names the offending field.
}

TEST_CASE(
    "Validator::clientRegexValidation passes through keys without a validator",
    "[validator][regex]")  // Unknown fields are allowed.
{
  jsoncons::json data = makeJsonObject(
      {{"first_name", "anything at all"}});  // A field with no specific rule.

  HttpError error;  // Receives any error.
  std::unordered_set<std::pair<std::string, std::string>>
      db_data;  // Receives validated pairs.
  REQUIRE(Validator::clientRegexValidation(data,
                                           error,
                                           db_data));  // Still succeeds.
  CHECK(db_data.count({"first_name", "anything at all"}) ==
        1);  // The pair is passed through unchanged.
}

TEST_CASE("Types::Search_t parses a valid search request",
          "[validator][search]")  // Full valid search object.
{
  // A complete, well-formed search request.
  jsoncons::json search_j = jsoncons::json::parse(R"({
        "keyword": "fever",
        "filter": "name",
        "order_by": "created_at",
        "direction": 0,
        "limit": 10,
        "offset": 20
    })");

  bool                  success = false;  // Set true by the parser on success.
  const Types::Search_t search(search_j, success);  // Parse the JSON.

  CHECK(success);                          // Parsing succeeded.
  CHECK(search.keyword == "fever");        // Keyword parsed.
  CHECK(search.filter == "name");          // Filter parsed.
  CHECK(search.order_by == "created_at");  // Order field parsed.
  CHECK(search.direction == "ASC");        // direction 0 maps to ASC
  CHECK(search.limit == 10);               // Limit parsed.
  CHECK(search.offset == 20);              // Offset parsed.
}

TEST_CASE("Types::Search_t direction 1 maps to DESC",
          "[validator][search]")  // Direction mapping.
{
  // Another search request, this time descending.
  jsoncons::json search = jsoncons::json::parse(R"({
        "keyword": "x", "filter": "f", "order_by": "o",
        "direction": 1, "limit": 1, "offset": 0
    })");

  bool                  success = false;  // Set true by the parser on success.
  const Types::Search_t parsed(search, success);  // Parse the JSON.

  CHECK(success);                     // Parsing succeeded.
  CHECK(parsed.direction == "DESC");  // Direction 1 becomes the string "DESC".
}

TEST_CASE("Types::Search_t reports failure for a missing key without throwing",
          "[validator][search]")  // Malformed input is handled gracefully.
{
  // The constructor catches exceptions internally and signals failure via
  // success=false rather than re-throwing, so callers can reply with a 400
  // instead of a 500. A search request with the "offset" field omitted.
  jsoncons::json incomplete = jsoncons::json::parse(R"({
        "keyword": "x", "filter": "f", "order_by": "o", "direction": 0, "limit": 1
    })");  // "offset" missing

  bool success = true;  // Start true so the parser must explicitly flip it.
  REQUIRE_NOTHROW(
      Types::Search_t(incomplete, success));  // Construction must not throw.
  CHECK_FALSE(success);  // But failure is reported via the flag.
}

// filter and order_by are interpolated into the SQL text, so only simple
// identifiers are allowed; anything containing SQL syntax, dots, leading digits
// or over-long names must be rejected to prevent injection.
TEST_CASE(
    "Types::Search_t only accepts plain identifiers for filter and order_by",
    "[validator][search]")  // Injection guard.
{
  // Returns true only when Search_t accepts both fields as valid identifiers.
  const auto parse =
      [](const std::string& filter,
         const std::string&
             order_by)  // Helper that builds a request and parses it.
  {
    jsoncons::json search_j;                   // Build the payload.
    search_j["keyword"]           = "x";       // A benign keyword.
    search_j["filter"]            = filter;    // The filter under test.
    search_j["order_by"]          = order_by;  // The order field under test.
    search_j["direction"]         = 0;         // Ascending.
    search_j["limit"]             = 1;         // One row.
    search_j["offset"]            = 0;         // From the start.
    bool                  success = false;     // Set by the parser.
    const Types::Search_t parsed(search_j, success);  // Parse it.
    return success;  // Report whether it was accepted.
  };

  CHECK(parse("name", "created_at"));  // Plain identifiers are accepted.
  CHECK(parse("_private2",
              "id"));  // Leading underscore and digits inside are fine.

  CHECK_FALSE(parse("name; DROP TABLE users",
                    "id"));  // SQL statement injection is rejected.
  CHECK_FALSE(
      parse("name",
            "id,(SELECT pg_sleep(5))"));  // Subquery injection is rejected.
  CHECK_FALSE(parse("name", "id DESC"));  // Embedded SQL keyword is rejected.
  CHECK_FALSE(parse("users.password",
                    "id"));  // A qualified name with a dot is rejected.
  CHECK_FALSE(parse("1name", "id"));  // A leading digit is rejected.
  CHECK_FALSE(parse("", "id"));       // An empty identifier is rejected.
  CHECK_FALSE(parse("name", ""));     // Empty order field is rejected.
  CHECK_FALSE(parse(std::string(64, 'a'),
                    "id"));  // An over-long identifier is rejected.
}

// Bug 3 fixed: getDatabaseSchemaForTable() now returns {} early when the table
// is not found, so found=false is preserved and the end-iterator is never
// dereferenced.
TEST_CASE(
    "Validator::validateDatabaseCreateSchema returns false for an unknown "
    "table without crashing",
    "[validator]")  // Regression guard for undefined behaviour.
{
  // With an empty DatabaseSchema (no DB connected), every table name is
  // unknown. The fix ensures we get false + an error message instead of UB.
  Validator            v;        // The validator under test.
  api::v2::Http::Error error{};  // Receives the error message.
  jsoncons::json       data = jsoncons::json::parse(
      R"({"name":"test"})");  // A simple payload to validate.
  Validator::Rule rule(Validator::Rule::Action::NONE,
                       {});  // A rule with no actions.

  bool result =
      v.validateDatabaseCreateSchema("nonexistent_table_xyz",
                                     data,
                                     error,
                                     rule);  // Validate against a table that
                                             // cannot be found.
  CHECK_FALSE(result);                 // It returns false rather than crashing.
  CHECK_FALSE(error.message.empty());  // And explains why.
}

TEST_CASE("Types::Search_t rejects fields with wrong JSON types",
          "[validator][search]")  // Type checking of fields.
{
  SECTION("keyword is a number, not a string")  // keyword must be text.
  {
    // Here "keyword" is a number instead of a string.
    const jsoncons::json search_j = jsoncons::json::parse(R"({
            "keyword": 123, "filter": "f", "order_by": "o",
            "direction": 0, "limit": 1, "offset": 0
        })");
    bool                 success  = true;  // Parser must flip this to false.
    Types::Search_t      parsed(search_j, success);  // Parse it.
    CHECK_FALSE(success);                            // Wrong type is rejected.
  }

  SECTION("direction is a string, not a number")  // direction must be numeric.
  {
    // Here "direction" is a string instead of a number.
    const jsoncons::json search_j = jsoncons::json::parse(R"({
            "keyword": "x", "filter": "f", "order_by": "o",
            "direction": "ASC", "limit": 1, "offset": 0
        })");
    bool                 success  = true;  // Parser must flip this to false.
    Types::Search_t      parsed(search_j, success);  // Parse it.
    CHECK_FALSE(success);                            // Wrong type is rejected.
  }

  SECTION("limit is a string, not a number")  // limit must be numeric.
  {
    // Here "limit" is a string instead of a number.
    const jsoncons::json search_j = jsoncons::json::parse(R"({
            "keyword": "x", "filter": "f", "order_by": "o",
            "direction": 0, "limit": "ten", "offset": 0
        })");
    bool                 success  = true;  // Parser must flip this to false.
    Types::Search_t      parsed(search_j, success);  // Parse it.
    CHECK_FALSE(success);                            // Wrong type is rejected.
  }
}

TEST_CASE("Types::Search_t with zero limit and offset is valid",
          "[validator][search]")  // Zero is a legal value.
{
  // A search with zero limit and offset.
  const jsoncons::json  search_j = jsoncons::json::parse(R"({
        "keyword": "", "filter": "all", "order_by": "id",
        "direction": 0, "limit": 0, "offset": 0
    })");
  bool                  success  = false;           // Set by the parser.
  const Types::Search_t parsed(search_j, success);  // Parse it.
  CHECK(success);                                   // Zero values are accepted.
  CHECK(parsed.limit == 0);                         // Limit remains zero.
  CHECK(parsed.offset == 0);                        // Offset remains zero.
  CHECK(parsed.direction == "ASC");  // Direction 0 still maps to ASC.
}
