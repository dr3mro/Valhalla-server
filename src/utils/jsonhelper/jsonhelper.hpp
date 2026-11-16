/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file jsonhelper.hpp
 * @brief Conversions between JsonCpp, jsoncons and response strings.
 * @details Declares the JsonHelper class, a stateless toolbox with three static
 *          functions: one wraps a text message into a JsonCpp object with a
 *          "Message" key, and two others turn JsonCpp or jsoncons values into
 *          pretty-printed strings. All constructors and assignments are
 * deleted, so the class exists purely as a namespace for its helper functions.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The codebase uses two JSON libraries: JsonCpp for HTTP response bodies (via
 * Drogon) and jsoncons for validation work. This helper bridges them and gives
 * the rest of the server one consistent way to build simple status/error bodies
 * and to turn JSON values into readable text. Without it, each caller would
 * duplicate the wrapping and stringifying logic and the output shape could
 * drift.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static member functions:
 * https://en.cppreference.com/w/cpp/language/static#Static_member_functions
 * - Deleted functions (= delete):
 * https://en.cppreference.com/w/cpp/language/member_functions#Deleted_functions
 * - Function overloading (two stringify versions):
 * https://en.cppreference.com/w/cpp/language/overload_resolution
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.
// JsonCpp's Json::Value type used for HTTP response bodies.
#include <json/value.h>

// jsoncons' json type used by the validation code.
#include <jsoncons/json.hpp>
namespace api::v2  // Groups this utility with the version-2 API code.
{
  /**
   * @class JsonHelper
   * @brief Stateless bridge between the two JSON libraries and response
   * bodies.
   * @details The codebase uses JsonCpp for HTTP responses (Drogon) and
   * jsoncons for validation. This utility wraps simple message objects and
   *          serialises either representation to a pretty-printed string.
   *          All members are static and construction is deleted: there is no
   *          state and no thread-safety concern beyond the underlying
   * libraries.
   * @see Validator
   */
  class JsonHelper  // A class used only through static functions; it is never
                    // instantiated.
  {
   public:
    /**
     * No instances: the class is a namespace for static helpers.
     */
    JsonHelper() =
        delete;  // Deleted so no one can create an object of this type.
    /**
     * @brief Destruction is disabled; instances cannot exist.
     */
    virtual ~JsonHelper() = delete;  // No destructor is needed because
                                     // there are never any instances.

    /**
     * @brief Move construction is disabled; the class is never
     * instantiated.
     */
    JsonHelper(JsonHelper&&) =
        delete;  // Move would require an instance, which is forbidden.
    /**
     * @brief Copy construction is disabled; the class is never
     * instantiated.
     */
    JsonHelper(const JsonHelper&) =
        delete;  // Copy would require an instance, which is forbidden.

    /**
     * @brief Move assignment is disabled; the class is never instantiated.
     */
    JsonHelper& operator=(JsonHelper&&) =
        delete;  // Assignment is meaningless for a stateless helper.
    /**
     * @brief Copy assignment is disabled; the class is never instantiated.
     */
    JsonHelper& operator=(const JsonHelper&) =
        delete;  // Assignment is meaningless for a stateless helper.

    /**
     * @brief Wraps a message in a JSON object with a single "Message" key.
     * @param[in] message Text to place in the object, often an error or
     * status string.
     * @return A JsonCpp value suitable for a Drogon response body.
     */
    static Json::Value jsonify(
        const std::string&
            message);  // Builds { "Message": <text> } for simple replies.

    /**
     * @brief Serialises a JsonCpp value to a formatted string.
     * @param[in] json Value to serialise.
     * @return Pretty-printed JSON text (JsonCpp's `toStyledString`).
     */
    static std::string stringify(
        const Json::Value& json);  // Converts a JsonCpp value into
                                   // indented, human-readable text.

    /**
     * @brief Serialises a jsoncons value to a formatted string.
     * @param[in] json Value to serialise.
     * @return Pretty-printed JSON text (jsoncons `dump_pretty`).
     */
    static std::string stringify(
        const jsoncons::json& json);  // Overload that does the same for the
                                      // other JSON library's type.
  };
}  // namespace api::v2
