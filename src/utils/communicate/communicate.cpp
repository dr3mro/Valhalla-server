/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file communicate.cpp
 * @brief Implementation for communicate.
 * @details Implements Communicate::sendRequest, which allocates a cURL handle
 * and header list, builds the URL, chooses GET or POST through a small lookup
 *          table of lambdas, performs the transfer and cleans up.
 * handleResponse registers WriteCallback so incoming bytes are appended to a
 * string, and WriteCallback does that append. Failures are logged and returned
 * as empty optionals instead of thrown exceptions.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the actual outbound HTTP work happens. Callers get a simple
 * "send this request, give me the body" interface while all the messy libcurl
 * setup, method selection, resource cleanup and error logging stay here.
 * Because every failure is converted to std::nullopt, callers do not need
 * try/catch blocks of their own. Without this file the service-to-service
 * calling code would have to be repeated and carefully cleaned up at every call
 * site.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - libcurl easy API: https://curl.se/libcurl/c/curl_easy_init.html
 * - std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Lambdas with captures ([&curl, &data]):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - std::optional::value_or:
 * https://en.cppreference.com/w/cpp/utility/optional/value_or
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Declarations of the three Communicate functions implemented here.
#include "utils/communicate/communicate.hpp"

#include <curl/curl.h>   // The libcurl C functions used for the HTTP transfer.
#include <fmt/format.h>  // fmt::format builds the URL and error messages.

#include <string>  // std::string holds the URL, request body and response.

// Message is used to log failures to the console.
#include "utils/message/message.hpp"

std::optional<std::string>
Communicate::sendRequest(  // Returns the response body, or nothing on any
                           // failure.
    const std::string&        server,
    const int                 port,
    const std::string&        path,
    const drogon::HttpMethod& method,
    const std::string& data)  // Address, verb and body of the request to send.
{
  // Both libcurl resources are allocated before the try block so a single
  // cleanup path in the catch covers failures at any later stage.
  CURL* curl = nullptr;  // The cURL handle; starts null so cleanup code can
                         // tell whether it was created.
  struct curl_slist* headers =
      nullptr;  // Linked list of custom HTTP headers; starts empty.
  headers =
      curl_slist_append(headers,
                        "Content-Type: application/json");  // Adds one header
                                                            // saying the body
                                                            // is JSON.
  std::string response;  // Will hold the body received from the server.
  std::string url;       // Will hold the assembled "server:port/path" address.

  try
  {
    curl = curl_easy_init();  // Creates a fresh libcurl handle for this one
                              // request.
    if (curl == nullptr)      // libcurl can fail to initialise; check before
                              // using the handle.
    {
      throw std::runtime_error(
          "Failed to initialize cURL.");  // Turn the failure into an
                                          // exception for the single
                                          // catch block.
    }

    // Using fmt::format to construct the URL string
    url = fmt::format("{}:{}{}",
                      server,
                      port,
                      path);  // Joins server, port and path into a URL like
                              // "host:8080/api".
    curl_easy_setopt(curl,
                     CURLOPT_URL,
                     url.c_str());  // Tells cURL which URL to request; c_str
                                    // gives the C-style string it needs.
    curl_easy_setopt(curl,
                     CURLOPT_HTTPHEADER,
                     headers);  // Attaches the JSON content-type header to the
                                // request.

    // Map of methods to their respective cURL options
    const std::map<drogon::HttpMethod, std::function<void()>> methodMap =
        {{drogon::HttpMethod::Post,
          [&curl,
           &data]()  // Lambda for POST: captures curl and data by
                     // reference so it can configure them.
          {
            curl_easy_setopt(curl,
                             CURLOPT_POST,
                             1L);  // Mark this as a POST request.
            curl_easy_setopt(curl,
                             CURLOPT_POSTFIELDS,
                             data.c_str());  // Attach the request body; data
                                             // must outlive the call.
          }},
         {drogon::HttpMethod::Get,
          [&curl]()
          {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
          }}};  // Lambda for GET: mark it as a GET request (no body).

    auto method_opt =
        methodMap.find(method);  // Look up the requested verb in the map.
    if (method_opt !=
        methodMap
            .end())  // Only call the lambda if the verb was actually found.
    {
      method_opt->second();  // Call the appropriate method
    }
    else
    {
      throw std::invalid_argument(
          "Unsupported HTTP method: " +
          drogon::to_string(method));  // Reject verbs other than GET/POST.
    }

    // Handle response and potential errors. An empty body is treated as
    // failure even when the transfer itself reported success.
    response = handleResponse(curl).value_or(
        "");  // Run the transfer; value_or turns an empty optional into an
              // empty string.
    if (response.empty())  // An empty body means failure even if cURL
                           // itself reported success.
    {
      throw std::runtime_error(
          "Failed to get a valid response.");  // Escalate to the catch
                                               // block for cleanup and
                                               // logging.
    }

    // Release resources on the success path; the catch block covers the
    // failure path.
    curl_easy_cleanup(
        curl);  // Free the cURL handle now that the transfer is done.
    curl_slist_free_all(headers);  // Free the linked list of custom headers.
  }
  catch (const std::exception& e)  // Catches every exception thrown above,
                                   // including from handleResponse's callers.
  {
    Message::ErrorMessage(fmt::format("Failed to send request to {}.",
                                      url));  // Log which URL failed.
    Message::CriticalMessage(
        e.what());  // Log the underlying error text at critical level.

    if (curl != nullptr)  // Only free the handle if it was actually created.
    {
      curl_easy_cleanup(curl);  // Free the cURL handle on the failure path too.
    }
    curl_slist_free_all(headers);  // Always free the header list.
    return std::nullopt;  // Signal failure to the caller without throwing.
  }

  return response;  // Success: hand the response body back to the caller.
}
std::optional<std::string> Communicate::handleResponse(
    CURL*
        curl)  // Performs the transfer configured on curl and returns the body.
{
  std::string response;  // Accumulates the bytes received from the server.

  try
  {
    curl_easy_setopt(curl,
                     CURLOPT_WRITEFUNCTION,
                     WriteCallback);  // Register the function libcurl should
                                      // call with incoming data.
    curl_easy_setopt(curl,
                     CURLOPT_WRITEDATA,
                     &response);  // Pass our string as the user data given to
                                  // that callback.

    CURLcode res = curl_easy_perform(
        curl);  // Actually send the request; this blocks until it finishes.
    if (res != CURLE_OK)  // Any non-OK cURL code means the transfer failed.
    {
      throw std::runtime_error(
          "cURL request failed: " +
          std::string(curl_easy_strerror(res)));  // Convert the code to a
                                                  // readable message and throw.
    }
  }
  catch (const std::exception&
             e)  // Catches transfer errors so callers never see an exception.
  {
    Message::ErrorMessage(fmt::format(
        "Failed to handle request."));   // Log a generic failure notice.
    Message::CriticalMessage(e.what());  // Log the specific error text.
    response.clear();  // Clear the response to indicate failure
  }

  return response;  // Returns the body (possibly emptied by the catch block).
}

std::optional<size_t> Communicate::WriteCallback(
    void*  contents,
    size_t size,
    size_t nmemb,
    std::string*
        response)  // Called by libcurl for each chunk of received data.
{
  size_t totalSize = size * nmemb;  // libcurl passes item size and count;
                                    // their product is the byte count.
  response->append(static_cast<char*>(contents),
                   totalSize);  // Append those bytes to the receiving string.
  return totalSize;  // Report how many bytes were handled; libcurl expects
                     // this exact count.
}
