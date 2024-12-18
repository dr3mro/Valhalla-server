#pragma once
#include <drogon/drogon.h>
#include <json/value.h>

#include <jsoncons/json.hpp>

namespace api
{
    namespace v2
    {

        class Helper
        {
           public:
            Helper()          = default;
            virtual ~Helper() = default;
            static void failureResponse(const std::string&                                    message,
                                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
            static void errorResponse(const drogon::HttpStatusCode& code, const std::string& message,
                                      std::function<void(const drogon::HttpResponsePtr&)>&& callback);
            static void successResponse(const std::string&                                    message,
                                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

           private:
            static void        reply(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     const drogon::HttpStatusCode& code, const std::string& message);
            static std::string prepare(const std::string& status, const std::string& message);
        };
    }  // namespace v2
}  // namespace api