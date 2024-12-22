#pragma once

#include <string>
#include <unordered_map>

namespace api
{
    namespace v2
    {
        namespace Http
        {
            using Error = struct Error
            {
                int         code;
                std::string message;
            };

            enum Status : uint16_t
            {
                // Informational responses
                CONTINUE            = 100,
                SWITCHING_PROTOCOLS = 101,
                PROCESSING          = 102,

                // Successful responses
                OK                            = 200,
                CREATED                       = 201,
                ACCEPTED                      = 202,
                NON_AUTHORITATIVE_INFORMATION = 203,
                NO_CONTENT                    = 204,
                RESET_CONTENT                 = 205,
                PARTIAL_CONTENT               = 206,

                // Redirection messages
                MULTIPLE_CHOICES   = 300,
                MOVED_PERMANENTLY  = 301,
                FOUND              = 302,
                SEE_OTHER          = 303,
                NOT_MODIFIED       = 304,
                TEMPORARY_REDIRECT = 307,
                PERMANENT_REDIRECT = 308,

                // Client error responses
                BAD_REQUEST            = 400,
                UNAUTHORIZED           = 401,
                PAYMENT_REQUIRED       = 402,
                FORBIDDEN              = 403,
                NOT_FOUND              = 404,
                METHOD_NOT_ALLOWED     = 405,
                NOT_ACCEPTABLE         = 406,
                CONFLICT               = 409,
                GONE                   = 410,
                LENGTH_REQUIRED        = 411,
                PRECONDITION_FAILED    = 412,
                PAYLOAD_TOO_LARGE      = 413,
                UNSUPPORTED_MEDIA_TYPE = 415,

                // Server error responses
                INTERNAL_SERVER_ERROR      = 500,
                NOT_IMPLEMENTED            = 501,
                BAD_GATEWAY                = 502,
                SERVICE_UNAVAILABLE        = 503,
                GATEWAY_TIMEOUT            = 504,
                HTTP_VERSION_NOT_SUPPORTED = 505
            };

            // Utility function to get the description of a status code
            inline std::string getDescription(Status code)
            {
                // Utility: Map status codes to their descriptions
                static const std::unordered_map<Status, std::string> StatusDescriptions = {{CONTINUE, "Continue"}, {SWITCHING_PROTOCOLS, "Switching Protocols"},
                    {PROCESSING, "Processing"}, {OK, "OK"}, {CREATED, "Created"}, {ACCEPTED, "Accepted"},
                    {NON_AUTHORITATIVE_INFORMATION, "Non-Authoritative Information"}, {NO_CONTENT, "No Content"}, {RESET_CONTENT, "Reset Content"},
                    {PARTIAL_CONTENT, "Partial Content"}, {MULTIPLE_CHOICES, "Multiple Choices"}, {MOVED_PERMANENTLY, "Moved Permanently"}, {FOUND, "Found"},
                    {SEE_OTHER, "See Other"}, {NOT_MODIFIED, "Not Modified"}, {TEMPORARY_REDIRECT, "Temporary Redirect"},
                    {PERMANENT_REDIRECT, "Permanent Redirect"}, {BAD_REQUEST, "Bad Request"}, {UNAUTHORIZED, "Unauthorized"},
                    {PAYMENT_REQUIRED, "Payment Required"}, {FORBIDDEN, "Forbidden"}, {NOT_FOUND, "Not Found"}, {METHOD_NOT_ALLOWED, "Method Not Allowed"},
                    {NOT_ACCEPTABLE, "Not Acceptable"}, {CONFLICT, "Conflict"}, {GONE, "Gone"}, {LENGTH_REQUIRED, "Length Required"},
                    {PRECONDITION_FAILED, "Precondition Failed"}, {PAYLOAD_TOO_LARGE, "Payload Too Large"}, {UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type"},
                    {INTERNAL_SERVER_ERROR, "Internal Server Error"}, {NOT_IMPLEMENTED, "Not Implemented"}, {BAD_GATEWAY, "Bad Gateway"},
                    {SERVICE_UNAVAILABLE, "Service Unavailable"}, {GATEWAY_TIMEOUT, "Gateway Timeout"},
                    {HTTP_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported"}};

                auto description = StatusDescriptions.find(code);
                if (description != StatusDescriptions.end())
                {
                    return description->second;
                }
                return "Unknown Status Code";
            }
        }  // namespace Http
    }  // namespace v2

}  // namespace api
