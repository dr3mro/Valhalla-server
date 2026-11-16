/**
 * @file communicate.hpp
 * @brief Synchronous HTTP helper for service-to-service requests.
 * @details Declares the Communicate class, a static-only wrapper around libcurl
 *          for making outbound HTTP calls. sendRequest builds a URL from a
 * server, port and path, sends a JSON content-type header, and performs either
 * a GET or a POST. Two private helpers do the work: handleResponse runs the
 *          cURL transfer and WriteCallback receives the returned bytes. All
 * calls are blocking and failures are reported as an empty std::optional.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * When one part of the system needs to talk to another service over HTTP (for
 * example a separate microservice), it uses Communicate instead of calling
 * libcurl itself. That keeps the awkward C-style cURL setup in one place and
 * gives callers a simple, safe interface: pass the address and data, get back
 * an optional string. Without it, every caller would repeat handle setup,
 * cleanup and error handling.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - libcurl C API: https://curl.se/libcurl/c/
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Callbacks / function pointers:
 * https://en.cppreference.com/w/cpp/language/pointer#Pointers_to_functions
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.
// libcurl's C types (CURL, curl_slist) used by the private helpers.
#include <curl/curl.h>
// Provides drogon::HttpMethod, the HTTP verb type passed to sendRequest.
#include <drogon/drogon.h>

// std::optional is the return type that can hold a response or "no response".
#include <optional>
// std::string holds the server, path, body and response text.
#include <string>

/**
 * @class Communicate
 * @brief Thin static-only wrapper around libcurl for outbound HTTP calls.
 * @details Builds `server:port/path`, sends a JSON content-type header and
 *          performs either a GET or a POST with the supplied body. There is no
 *          instance state: each call creates and cleans up its own CURL handle,
 *          and failures are reported to the console via Message and signalled
 *          to the caller by an empty optional rather than by an exception.
 * @note Only GET and POST are supported; all calls are blocking.
 */
class Communicate  // A class used only through static functions; it has no
                   // instance state.
{
 public:
  /**
   * @brief Sends an HTTP request and returns the response body.
   * @param[in] server Host name or address of the target service.
   * @param[in] port TCP port of the target service.
   * @param[in] path Request path, including any query string.
   * @param[in] method HTTP verb; only Get and Post are recognised.
   * @param[in] data Request body, used for POST (ignored for GET).
   * @return Response body on success, `std::nullopt` on any failure.
   * @note All exceptions (init failure, unsupported method, cURL error, empty
   *       body) are caught internally, logged via Message and converted to
   *       `std::nullopt`; nothing is thrown to the caller.
   * @warning `data` is passed to libcurl by pointer, so it must outlive the
   *          call; this holds for normal synchronous use.
   */
  static std::optional<std::string>
  sendRequest(  // The one public entry point: an optional string that is
                // empty on failure.
      const std::string&        server,
      int                       port,
      const std::string&        path,
      const drogon::HttpMethod& method,
      const std::string& data);  // Arguments describe where to send, which
                                 // verb to use and what body to send.

 private:
  /**
   * @brief Executes the prepared cURL handle and captures the response body.
   * @param[in,out] curl Handle already configured with URL, method and
   * headers.
   * @return Accumulated body, or an empty string on error.
   * @note A cURL error or thrown exception is logged via Message and produces
   *       an empty string; callers treat that as failure.
   */
  static std::optional<std::string> handleResponse(
      CURL* curl);  // Runs the transfer and returns whatever the server sent
                    // back.

  /**
   * @brief libcurl write callback that appends received bytes to a string.
   * @param[in] contents Pointer to the received data block.
   * @param[in] size Size of each item.
   * @param[in] nmemb Number of items.
   * @param[out] response String to append to; must remain valid for the
   * transfer.
   * @return Number of bytes consumed, which must equal `size * nmemb` or
   *         libcurl reports a write error.
   */
  static std::optional<size_t> WriteCallback(
      void*        contents,
      size_t       size,
      size_t       nmemb,
      std::string* response);  // libcurl calls this repeatedly with chunks of
                               // the response; it appends them to the string.
};
