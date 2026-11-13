#include <json/value.h>

#include <catch2/catch_test_macros.hpp>
#include <jsoncons/json.hpp>
#include <string>

#include "utils/jsonhelper/jsonhelper.hpp"

using JsonHelper = api::v2::JsonHelper;

TEST_CASE("JsonHelper::jsonify wraps a message", "[utils][jsonhelper]")
{
    const Json::Value wrapped = JsonHelper::jsonify("hello world");

    REQUIRE(wrapped.isMember("Message"));
    CHECK(wrapped["Message"].asString() == "hello world");
}

TEST_CASE("JsonHelper::stringify serialises a JsonCpp value", "[utils][jsonhelper]")
{
    Json::Value value = JsonHelper::jsonify("the message");
    value["Status"]   = "Failure";

    const std::string text = JsonHelper::stringify(value);

    CHECK(text.find("\"Message\" : \"the message\"") != std::string::npos);
    CHECK(text.find("\"Status\" : \"Failure\"") != std::string::npos);
}

TEST_CASE("JsonHelper::stringify serialises a jsoncons value", "[utils][jsonhelper]")
{
    jsoncons::json object = jsoncons::json::object();
    object["key"]         = "value";
    object["answer"]      = 42;

    const std::string text = JsonHelper::stringify(object);

    // dump_pretty output: the keys and values must all be present.
    CHECK(text.find("\"key\"") != std::string::npos);
    CHECK(text.find("\"value\"") != std::string::npos);
    CHECK(text.find("42") != std::string::npos);
}

TEST_CASE("JsonHelper::stringify round-trips through jsoncons parse", "[utils][jsonhelper]")
{
    jsoncons::json object = jsoncons::json::object();
    object["Message"]     = "logout ok";

    const auto reparsed = jsoncons::json::parse(JsonHelper::stringify(object));
    CHECK(reparsed.at("Message").as<std::string>() == "logout ok");
}

TEST_CASE("JsonHelper::jsonify handles an empty string message", "[utils][jsonhelper]")
{
    const Json::Value wrapped = JsonHelper::jsonify("");

    REQUIRE(wrapped.isMember("Message"));
    CHECK(wrapped["Message"].asString() == "");
}

TEST_CASE("JsonHelper::stringify handles an empty jsoncons object", "[utils][jsonhelper]")
{
    const jsoncons::json empty = jsoncons::json::object();
    const std::string    text  = JsonHelper::stringify(empty);
    CHECK_FALSE(text.empty());
}

TEST_CASE("JsonHelper::stringify handles a jsoncons null", "[utils][jsonhelper]")
{
    const jsoncons::json null_j = jsoncons::json::null();
    CHECK_NOTHROW(JsonHelper::stringify(null_j));
}

TEST_CASE("JsonHelper::stringify handles an empty JsonCpp value", "[utils][jsonhelper]")
{
    const Json::Value empty;
    CHECK_NOTHROW(JsonHelper::stringify(empty));
}
