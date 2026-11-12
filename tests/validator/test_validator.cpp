#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <jsoncons/json.hpp>

#include "entities/base/types.hpp"
#include "utils/global/http.hpp"
#include "validator/validator.hpp"

using Validator = ::Validator;
using HttpError = api::v2::Http::Error;
namespace Types = api::v2::Types;

namespace
{
    jsoncons::json makeJsonObject(std::initializer_list<std::pair<const char *, std::string>> items)
    {
        jsoncons::json object = jsoncons::json::object();
        for (const auto &[key, value] : items)
        {
            object[key] = value;
        }
        return object;
    }
}  // namespace

TEST_CASE("Validator::Rule flag semantics", "[validator][rule]")
{
    using Action = Validator::Rule::Action;

    const Validator::Rule rule(Action::ASSERT_IMMUTABLE, {"id", "created_at"});

    SECTION("a key covered by the rule matches")
    {
        CHECK(rule.check(Action::ASSERT_IMMUTABLE, "id"));
        CHECK(rule.check(Action::ASSERT_IMMUTABLE, "created_at"));
    }

    SECTION("a key not covered does not match")
    {
        CHECK_FALSE(rule.check(Action::ASSERT_IMMUTABLE, "name"));
    }

    SECTION("a different action does not match even for covered keys")
    {
        CHECK_FALSE(rule.check(Action::ASSERT_NOT_PRESENT, "id"));
        CHECK_FALSE(rule.check(Action::IGNORE_IF_MISSING_FROM_SCHEMA, "id"));
    }

    SECTION("explicit operator bool reflects the action")
    {
        CHECK(static_cast<bool>(rule));
        const Validator::Rule none(Action::NONE, {"id"});
        CHECK_FALSE(static_cast<bool>(none));
    }

    SECTION("bit operators behave as flags")
    {
        auto flags = Action::ASSERT_IMMUTABLE;
        flags |= Action::ASSERT_NOT_PRESENT;
        CHECK((flags & Action::ASSERT_IMMUTABLE) == Action::ASSERT_IMMUTABLE);
        CHECK((flags & Action::ASSERT_NOT_PRESENT) == Action::ASSERT_NOT_PRESENT);
        CHECK((flags & Action::IGNORE_IF_MISSING_FROM_SCHEMA) == Action::NONE);
        CHECK((~Action::NONE) != Action::NONE);
    }
}

TEST_CASE("Validator::Rule keeps key sets per action", "[validator][rule]")
{
    using Action = Validator::Rule::Action;

    // The keys map is keyed by action, so a rule can cover several actions.
    Validator::Rule rule(Action::ASSERT_IMMUTABLE, {"id"});

    // The public constructor stores the set under the rule's action.
    CHECK(rule.check(Action::ASSERT_IMMUTABLE, "id"));
    CHECK_FALSE(rule.check(Action::ASSERT_IMMUTABLE, "other"));
}

TEST_CASE("Validator::clientRegexValidation accepts known-good client data", "[validator][regex]")
{
    jsoncons::json data = makeJsonObject({
        {"username", "john_doe"},
        {"email", "john.doe@example.com"},
        {"dob", "31-12-1990"},
        {"gender", "Male"},
    });

    HttpError                                     error;
    std::unordered_set<std::pair<std::string, std::string>> db_data;
    REQUIRE(Validator::clientRegexValidation(data, error, db_data));

    // Every validated pair is passed through to db_data.
    CHECK(db_data.count({"username", "john_doe"}) == 1);
    CHECK(db_data.count({"email", "john.doe@example.com"}) == 1);
}

TEST_CASE("Validator::clientRegexValidation rejects values that do not match their pattern", "[validator][regex]")
{
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"username", "1starts_with_digit"},
        {"username", "Has Uppercase"},
        {"email", "not-an-email"},
        {"dob", "1990-12-31"},  // wrong separator order (expects dd-mm-yyyy)
        {"gender", "male"},     // case sensitive
    };

    for (const auto &[key, value] : invalid)
    {
        CAPTURE(key, value);

        jsoncons::json data = makeJsonObject({{key.c_str(), value}});

        HttpError                                     error;
        std::unordered_set<std::pair<std::string, std::string>> db_data;
        REQUIRE_FALSE(Validator::clientRegexValidation(data, error, db_data));
        CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
        CHECK_FALSE(error.message.empty());
    }
}

TEST_CASE("Validator::clientRegexValidation password policy", "[validator][regex]")
{
    const std::vector<std::string> valid = {
        "Abcdef1!",   // lower, upper, digit, special, 8 chars
        "Longer1Password!",
    };
    const std::vector<std::string> invalid = {
        "abcdef1!",   // no uppercase
        "ABCDEF1!",   // no lowercase
        "Abcdefgh!",  // no digit
        "Abcdef12",   // no special character
        "Ab1!",       // shorter than 8
    };

    for (const auto &password : valid)
    {
        CAPTURE(password);
        jsoncons::json data = makeJsonObject({{"password", password}});
        HttpError                                     error;
        std::unordered_set<std::pair<std::string, std::string>> db_data;
        CHECK(Validator::clientRegexValidation(data, error, db_data));
    }

    for (const auto &password : invalid)
    {
        CAPTURE(password);
        jsoncons::json data = makeJsonObject({{"password", password}});
        HttpError                                     error;
        std::unordered_set<std::pair<std::string, std::string>> db_data;
        CHECK_FALSE(Validator::clientRegexValidation(data, error, db_data));
        CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
    }
}

TEST_CASE("Validator::clientRegexValidation rejects empty values", "[validator][regex]")
{
    jsoncons::json data = makeJsonObject({{"username", ""}});

    HttpError                                     error;
    std::unordered_set<std::pair<std::string, std::string>> db_data;
    REQUIRE_FALSE(Validator::clientRegexValidation(data, error, db_data));
    CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
    CHECK(error.message.find("username") != std::string::npos);
}

TEST_CASE("Validator::clientRegexValidation passes through keys without a validator", "[validator][regex]")
{
    jsoncons::json data = makeJsonObject({{"first_name", "anything at all"}});

    HttpError                                     error;
    std::unordered_set<std::pair<std::string, std::string>> db_data;
    REQUIRE(Validator::clientRegexValidation(data, error, db_data));
    CHECK(db_data.count({"first_name", "anything at all"}) == 1);
}

TEST_CASE("Types::Search_t parses a valid search request", "[validator][search]")
{
    jsoncons::json search_j = jsoncons::json::parse(R"({
        "keyword": "fever",
        "filter": "name",
        "order_by": "created_at",
        "direction": 0,
        "limit": 10,
        "offset": 20
    })");

    bool      success = false;
    const Types::Search_t search(search_j, success);

    CHECK(success);
    CHECK(search.keyword == "fever");
    CHECK(search.filter == "name");
    CHECK(search.order_by == "created_at");
    CHECK(search.direction == "ASC");  // direction 0 maps to ASC
    CHECK(search.limit == 10);
    CHECK(search.offset == 20);
}

TEST_CASE("Types::Search_t direction 1 maps to DESC", "[validator][search]")
{
    jsoncons::json search = jsoncons::json::parse(R"({
        "keyword": "x", "filter": "f", "order_by": "o",
        "direction": 1, "limit": 1, "offset": 0
    })");

    bool success = false;
    const Types::Search_t parsed(search, success);

    CHECK(success);
    CHECK(parsed.direction == "DESC");
}

TEST_CASE("Types::Search_t reports failure for a missing key without throwing", "[validator][search]")
{
    // The constructor catches exceptions internally and signals failure via success=false
    // rather than re-throwing, so callers can reply with a 400 instead of a 500.
    jsoncons::json incomplete = jsoncons::json::parse(R"({
        "keyword": "x", "filter": "f", "order_by": "o", "direction": 0, "limit": 1
    })");  // "offset" missing

    bool success = true;
    REQUIRE_NOTHROW(Types::Search_t(incomplete, success));
    CHECK_FALSE(success);
}

// NOTE: Validator::validateDatabaseCreateSchema / validateDatabaseUpdateSchema
// / validateDatabaseReadSchema are intentionally NOT covered: with no
// PostgreSQL they fail schema lookup cleanly, but their shared
// getDatabaseSchemaForTable() dereferences the end iterator of the result
// set (it sets found=false and then unconditionally assigns found=true
// before dereferencing) - see tests/README.md, known bug 3. Exercising them
// is undefined behaviour until that is fixed.

TEST_CASE("Types::Search_t rejects fields with wrong JSON types", "[validator][search]")
{
    SECTION("keyword is a number, not a string")
    {
        const jsoncons::json search_j = jsoncons::json::parse(R"({
            "keyword": 123, "filter": "f", "order_by": "o",
            "direction": 0, "limit": 1, "offset": 0
        })");
        bool success = true;
        Types::Search_t parsed(search_j, success);
        CHECK_FALSE(success);
    }

    SECTION("direction is a string, not a number")
    {
        const jsoncons::json search_j = jsoncons::json::parse(R"({
            "keyword": "x", "filter": "f", "order_by": "o",
            "direction": "ASC", "limit": 1, "offset": 0
        })");
        bool success = true;
        Types::Search_t parsed(search_j, success);
        CHECK_FALSE(success);
    }

    SECTION("limit is a string, not a number")
    {
        const jsoncons::json search_j = jsoncons::json::parse(R"({
            "keyword": "x", "filter": "f", "order_by": "o",
            "direction": 0, "limit": "ten", "offset": 0
        })");
        bool success = true;
        Types::Search_t parsed(search_j, success);
        CHECK_FALSE(success);
    }
}

TEST_CASE("Types::Search_t with zero limit and offset is valid", "[validator][search]")
{
    const jsoncons::json search_j = jsoncons::json::parse(R"({
        "keyword": "", "filter": "all", "order_by": "id",
        "direction": 0, "limit": 0, "offset": 0
    })");
    bool success = false;
    const Types::Search_t parsed(search_j, success);
    CHECK(success);
    CHECK(parsed.limit == 0);
    CHECK(parsed.offset == 0);
    CHECK(parsed.direction == "ASC");
}
