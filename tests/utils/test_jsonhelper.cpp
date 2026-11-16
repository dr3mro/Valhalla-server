/**
 * @file test_jsonhelper.cpp
 * @brief Catch2 unit tests for JsonHelper, the bridge between the two JSON
 * libraries.
 * @details JsonHelper wraps a text message into a JsonCpp object with a
 * "Message" key, and serialises both JsonCpp and jsoncons values into
 * pretty-printed strings. These tests check the exact shape of that output,
 * that serialised text can be parsed back again, and that empty or null inputs
 * never throw.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * HTTP status and error bodies are built with jsonify() and turned into
 * response text with stringify(), so the "Message" shape is part of the
 * contract clients rely on. If a key name or its spacing changed, clients
 * parsing responses could break. The jsoncons overload supports validation
 * output, and a crash on null or empty input could take down a request handler.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertion macros (CHECK, REQUIRE, CHECK_NOTHROW):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - Static member functions:
 * https://en.cppreference.com/w/cpp/language/static#Static_member_functions
 * - Function overloading (two stringify versions):
 * https://en.cppreference.com/w/cpp/language/overload_resolution
 * - JsonCpp: https://github.com/open-source-parsers/jsoncpp
 * - jsoncons: https://github.com/danielaparker/jsoncons
 * - std::string: https://en.cppreference.com/w/cpp/string/basic_string
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// JsonCpp's Json::Value type, returned by jsonify and passed to stringify.
#include <json/value.h>

// Brings in TEST_CASE, CHECK, REQUIRE and CHECK_NOTHROW.
#include <catch2/catch_test_macros.hpp>
// jsoncons' json type, used by the validation side of the helper.
#include <jsoncons/json.hpp>
// std::string, the type of the serialised output and message input.
#include <string>

#include "utils/jsonhelper/jsonhelper.hpp"  // The JsonHelper class under test.

using JsonHelper = api::v2::JsonHelper;  // Short local alias so the calls below
                                         // are easier to read.

// jsonify must produce an object containing a "Message" key holding our text.
TEST_CASE("JsonHelper::jsonify wraps a message", "[utils][jsonhelper]") {
  const Json::Value wrapped = JsonHelper::jsonify(
      "hello world");  // Build the immutable wrapper object.

  REQUIRE(wrapped.isMember(
      "Message"));  // Hard requirement: without this key the response shape
                    // is wrong, so stop here if absent.
  CHECK(wrapped["Message"].asString() ==
        "hello world");  // The value under that key must be our original text.
}

// stringify must emit JsonCpp's styled text with the exact spacing clients may
// depend on.
TEST_CASE("JsonHelper::stringify serialises a JsonCpp value",
          "[utils][jsonhelper]") {
  Json::Value value =
      JsonHelper::jsonify("the message");  // Start from a wrapper so the
                                           // object has a Message key.
  value["Status"] =
      "Failure";  // Add a second field to prove both survive serialisation.

  const std::string text =
      JsonHelper::stringify(value);  // Turn the object into formatted text.

  // JsonCpp's styled writer emits "key" : value with spaces around the
  // colon; these substrings pin that exact wire format down.
  CHECK(text.find("\"Message\" : \"the message\"") !=
        std::string::npos);  // find(...) != npos means the substring is
                             // present in text.
  CHECK(text.find("\"Status\" : \"Failure\"") !=
        std::string::npos);  // The added field must also appear in the output.
}

// The jsoncons overload must also produce readable text containing every key
// and value.
TEST_CASE("JsonHelper::stringify serialises a jsoncons value",
          "[utils][jsonhelper]") {
  jsoncons::json object =
      jsoncons::json::object();  // Create an empty JSON object (as opposed to
                                 // an array or null).
  object["key"]    = "value";    // Add a member whose value is a string.
  object["answer"] = 42;         // Add a member whose value is a number.

  const std::string text = JsonHelper::stringify(
      object);  // Serialise through the jsoncons dump_pretty overload.

  // dump_pretty output: the keys and values must all be present.
  CHECK(text.find("\"key\"") !=
        std::string::npos);  // The first key name appears in the text.
  CHECK(text.find("\"value\"") !=
        std::string::npos);                     // Its string value appears too.
  CHECK(text.find("42") != std::string::npos);  // The number appears as well.
}

// What stringify writes, jsoncons::parse must read back - a classic round-trip
// check.
TEST_CASE("JsonHelper::stringify round-trips through jsoncons parse",
          "[utils][jsonhelper]") {
  jsoncons::json object =
      jsoncons::json::object();  // Build a small object with one member.
  object["Message"] =
      "logout ok";  // Use the key the server uses for status messages.

  const auto reparsed = jsoncons::json::parse(JsonHelper::stringify(
      object));  // Serialise, then immediately parse the text back.
  CHECK(reparsed.at("Message").as<std::string>() ==
        "logout ok");  // at() fetches the key; the value must match exactly.
}

// An empty message is still a valid message: the key must exist with an empty
// string value.
TEST_CASE("JsonHelper::jsonify handles an empty string message",
          "[utils][jsonhelper]") {
  const Json::Value wrapped = JsonHelper::jsonify("");  // Wrap an empty string.

  REQUIRE(wrapped.isMember("Message"));  // The key must still be present.
  CHECK(wrapped["Message"].asString() ==
        "");  // ...and its value must be the empty string.
}

// Serialising an object with no members must not fail or produce an empty
// buffer.
TEST_CASE("JsonHelper::stringify handles an empty jsoncons object",
          "[utils][jsonhelper]") {
  const jsoncons::json empty =
      jsoncons::json::object();  // A valid but member-less JSON object.
  const std::string text = JsonHelper::stringify(empty);  // Serialise it.
  CHECK_FALSE(text.empty());  // Even "{}" is non-empty text, so a blank
                              // result would be a bug.
}

// JSON null is a legitimate value; serialising it must not throw.
TEST_CASE("JsonHelper::stringify handles a jsoncons null",
          "[utils][jsonhelper]") {
  const jsoncons::json null_j =
      jsoncons::json::null();  // Build a JSON null (not an object or an
                               // array).
  CHECK_NOTHROW(JsonHelper::stringify(
      null_j));  // CHECK_NOTHROW fails if any exception escapes the call.
}

// A default-constructed Json::Value is JsonCpp's "null" state; stringify must
// handle that too.
TEST_CASE("JsonHelper::stringify handles an empty JsonCpp value",
          "[utils][jsonhelper]") {
  const Json::Value empty;  // Default construction gives the null value.
  CHECK_NOTHROW(JsonHelper::stringify(empty));  // No exception may escape.
}
