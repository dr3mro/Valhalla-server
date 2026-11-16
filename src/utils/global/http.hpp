/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file http.hpp
 * @brief HTTP status codes and error payloads shared by the API layer.
 * @details Defines the Error struct (a numeric code plus a message) and the
 *          Status enum, which lists the HTTP response codes this server uses.
 *          The enum is grouped into the standard families: 1xx informational,
 *          2xx success, 3xx redirection, 4xx client error and 5xx server error.
 *          A small helper, getDescription, turns a Status value into its
 * standard human-readable reason phrase (for example "OK" or "Not Found").
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Every callback in the API reports its outcome as an integer status code, and
 * this file is the single vocabulary of those codes. Controllers, the error
 * macros and the response builders all use Status values so a "404" means the
 * same thing everywhere. getDescription lets the server include a readable
 * reason phrase when needed. Without this shared list, status codes would be
 * magic numbers scattered through the code with no consistent meaning.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Scoped/unscoped enums: https://en.cppreference.com/w/cpp/language/enum
 * - std::unordered_map:
 * https://en.cppreference.com/w/cpp/container/unordered_map
 * - Function-local static variables:
 * https://en.cppreference.com/w/cpp/language/storage_duration
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

// std::string is used for Error::message and the reason phrases.
#include <string>
// The status-to-description lookup table uses std::unordered_map.
#include <unordered_map>

namespace api::v2::Http  // Groups HTTP-related types with the version-2 API
                         // code.
{
  /**
   * @brief Validation/error payload: a numeric code and a human-readable
   * message.
   * @details Passed by reference through validator APIs that report failures
   *          without throwing; callers populate both members.
   */
  using Error = struct Error  // A lightweight named struct (the "using" alias
                              // keeps the old spelling working).
  {
    int code;  // Numeric problem code chosen by the validator; useful for
               // machines.
    std::string message;  // Human-readable explanation of the problem;
                          // useful for people.
  };

  /**
   * @enum Status
   * @brief HTTP status codes returned as the first CALLBACK_ argument.
   * @details Enumerators mirror standard HTTP codes, grouped below by class:
   *          1xx informational, 2xx success, 3xx redirection, 4xx client
   * error and 5xx server error. INTERNAL_SERVER_ERROR and SERVICE_UNAVAILABLE
   *          are the two values produced by statusForException.
   * @note Only codes used by this server are listed; getDescription falls
   * back to a generic string for values outside this set.
   */
  enum Status : uint16_t  // Stored as a 16-bit unsigned number; codes like
                          // 503 fit easily and the type is compact.
  {
    // Informational responses
    CONTINUE = 100,  // Server got the request headers; client should go
                     // ahead and send the body.
    SWITCHING_PROTOCOLS = 101,  // Server is switching to a different
                                // protocol as the client requested.
    PROCESSING = 102,  // Server accepted the request but has not produced a
                       // response yet.

    // Successful responses
    OK         = 200,  // The general "it worked" answer.
    CREATED    = 201,  // The request worked and a new resource was created.
    ACCEPTED   = 202,  // Request accepted for processing, but the work is not
                       // finished yet.
    NON_AUTHORITATIVE_INFORMATION =
        203,  // Returned information may differ from the original source.
    NO_CONTENT        = 204,  // Success, but there is no body to send back.
    RESET_CONTENT     = 205,  // Success; the client should reset whatever view
                              // it is showing.
    PARTIAL_CONTENT   = 206,  // Only part of the resource is returned (used
                              // for ranged downloads).

    // Redirection messages
    MULTIPLE_CHOICES  = 300,  // There are several possible responses; the
                              // client should choose.
    MOVED_PERMANENTLY = 301,  // The resource has a new permanent address.
    FOUND        = 302,  // The resource is temporarily at a different address.
    SEE_OTHER    = 303,  // The answer is at another address; fetch it with GET.
    NOT_MODIFIED = 304,  // The resource has not changed; the client can use
                         // its cached copy.
    TEMPORARY_REDIRECT = 307,  // Redirect for now; repeat the same method
                               // at the new address.
    PERMANENT_REDIRECT = 308,  // Redirect permanently; repeat the same
                               // method at the new address.

    // Client error responses
    BAD_REQUEST        = 400,  // The server could not understand the request.
    UNAUTHORIZED       = 401,  // The client must log in / authenticate first.
    PAYMENT_REQUIRED   = 402,  // Reserved for future use (digital payments).
    FORBIDDEN          = 403,  // The server understood but refuses to allow it.
    NOT_FOUND          = 404,  // The requested resource does not exist.
    METHOD_NOT_ALLOWED =
        405,               // That HTTP method is not allowed for this resource.
    NOT_ACCEPTABLE = 406,  // The server cannot produce a response the
                           // client will accept.
    CONFLICT = 409,  // The request conflicts with the resource's current state.
    GONE     = 410,  // The resource used to exist but is permanently gone.
    LENGTH_REQUIRED = 411,  // The request must include a Content-Length header.
    PRECONDITION_FAILED =
        412,  // Conditions set in the request headers were not met.
    PAYLOAD_TOO_LARGE =
        413,  // The request body is bigger than the server allows.
    UNSUPPORTED_MEDIA_TYPE =
        415,  // The request's media type is not supported here.

    // Server error responses
    INTERNAL_SERVER_ERROR =
        500,  // A generic, unexpected failure on the server.
    NOT_IMPLEMENTED =
        501,  // The server does not support the requested feature yet.
    BAD_GATEWAY = 502,  // An upstream server sent back an invalid response.
    SERVICE_UNAVAILABLE = 503,  // The server is temporarily unavailable;
                                // the client should retry.
    GATEWAY_TIMEOUT     = 504,  // An upstream server took too long to respond.
    HTTP_VERSION_NOT_SUPPORTED =
        505  // The server does not support the HTTP version used.
  };

  // Utility function to get the description of a status code
  /**
   * @brief Returns the standard reason phrase for a status code.
   * @param[in] code Status code to describe.
   * @return Reason phrase (e.g. "OK"), or "Unknown Status Code" if `code`
   *         is not present in the lookup table.
   * @note The lookup table is a function-local static, initialised once and
   *       read-only thereafter.
   */
  inline std::string getDescription(
      Status code)  // Inline so the definition can live in the header; takes
                    // the code by value.
  {
    // Utility: Map status codes to their descriptions
    static const std::unordered_map<Status, std::string> StatusDescriptions = {
        {CONTINUE, "Continue"},
        {SWITCHING_PROTOCOLS,
            "Switching Protocols"},  // A read-only table built once,
                                     // pairing each code with its
                                     // standard reason phrase (the rows
                                     // continue below).
        {PROCESSING, "Processing"}, {OK, "OK"}, {CREATED, "Created"},
        {ACCEPTED, "Accepted"},
        {NON_AUTHORITATIVE_INFORMATION, "Non-Authoritative Information"},
        {NO_CONTENT, "No Content"}, {RESET_CONTENT, "Reset Content"},
        {PARTIAL_CONTENT, "Partial Content"},
        {MULTIPLE_CHOICES, "Multiple Choices"},
        {MOVED_PERMANENTLY, "Moved Permanently"}, {FOUND, "Found"},
        {SEE_OTHER, "See Other"}, {NOT_MODIFIED, "Not Modified"},
        {TEMPORARY_REDIRECT, "Temporary Redirect"},
        {PERMANENT_REDIRECT, "Permanent Redirect"},
        {BAD_REQUEST, "Bad Request"}, {UNAUTHORIZED, "Unauthorized"},
        {PAYMENT_REQUIRED, "Payment Required"}, {FORBIDDEN, "Forbidden"},
        {NOT_FOUND, "Not Found"}, {METHOD_NOT_ALLOWED, "Method Not Allowed"},
        {NOT_ACCEPTABLE, "Not Acceptable"}, {CONFLICT, "Conflict"},
        {GONE, "Gone"}, {LENGTH_REQUIRED, "Length Required"},
        {PRECONDITION_FAILED, "Precondition Failed"},
        {PAYLOAD_TOO_LARGE, "Payload Too Large"},
        {UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type"},
        {INTERNAL_SERVER_ERROR, "Internal Server Error"},
        {NOT_IMPLEMENTED, "Not Implemented"}, {BAD_GATEWAY, "Bad Gateway"},
        {SERVICE_UNAVAILABLE, "Service Unavailable"},
        {GATEWAY_TIMEOUT, "Gateway Timeout"},
        {HTTP_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported"}};

    auto description =
        StatusDescriptions.find(code);  // Look up the code; find returns an
                                        // iterator (or end() when missing).
    if (description !=
        StatusDescriptions
            .end())  // Only use the result if the code was actually found.
    {
      return description->second;  // The iterator's second member is the
                                   // reason phrase string.
    }
    return "Unknown Status Code";  // Fallback for codes that are not in the
                                   // table.
  }
}  // namespace api::v2::Http
