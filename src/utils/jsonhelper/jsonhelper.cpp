/**
 * @file jsonhelper.cpp
 * @brief Implementation for jsonhelper.
 * @details Implements the three static helpers declared in jsonhelper.hpp:
 *          jsonify creates a single-key JsonCpp object, one stringify overload
 *          uses jsoncons' dump_pretty, and the other uses JsonCpp's
 *          toStyledString. All are thin wrappers around the two libraries.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the actual conversion work lives. Callers across the API
 * include the header and call these functions to build uniform status/error
 * bodies and to print JSON for logs or responses. Keeping the implementations
 * here (rather than inline in the header) means callers do not need to depend
 * on the details of both JSON libraries just to use the helpers.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Function overloading:
 * https://en.cppreference.com/w/cpp/language/overload_resolution
 * - Qualified names (api::v2::JsonHelper::...):
 * https://en.cppreference.com/w/cpp/language/identifiers#Qualified_identifiers
 * - std::string: https://en.cppreference.com/w/cpp/string/basic_string
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations of the three JsonHelper static functions implemented below.
#include "jsonhelper.hpp"

// Fully-qualified name identifies which
// class's static function this is.
Json::Value api::v2::JsonHelper::jsonify(const std::string& message) {
  // Uniform single-key shape for simple status and error bodies.
  Json::Value object;  // An empty JsonCpp value; assigning to
                       // object["Message"] turns it into a JSON object.
  object["Message"] = message;  // Adds one key/value pair so the body is {
                                // "Message": "..." }.
  return object;  // Returns the object by value for the caller to use as a
                  // response body.
}
// Overload for the jsoncons type used by validation.
std::string api::v2::JsonHelper::stringify(const jsoncons::json& json) {
  // dump_pretty writes into the supplied string; used for validation results.
  std::string json_string;  // Empty receiving string; dump_pretty appends the
                            // formatted JSON to it.
  json.dump_pretty(
      json_string);    // jsoncons fills the string with indented JSON text.
  return json_string;  // Hands the finished text back to the caller.
}
std::string api::v2::JsonHelper::stringify(const Json::Value& json) {
  return json.toStyledString();
}  // JsonCpp builds the pretty text directly and returns it.
