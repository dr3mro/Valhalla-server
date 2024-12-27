#pragma once
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <json/value.h>

#include <functional>
#include <jsoncons/json.hpp>
#include <string>

namespace api::v2
{

    class Helper
    {
       public:
        Helper()                         = default;
        Helper(const Helper&)            = default;
        Helper(Helper&&)                 = delete;
        Helper& operator=(const Helper&) = default;
        Helper& operator=(Helper&&)      = delete;
        virtual ~Helper()                = default;

        static void failureResponse(const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback);
        static void errorResponse(
            const drogon::HttpStatusCode& code, const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback);
        static void successResponse(const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback);

       private:
        static void reply(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const drogon::HttpStatusCode& code, const std::string& message);
        static std::string prepare(const std::string& status, const std::string& message);

        static constexpr const char* FAILURE = "Failure";
        static constexpr const char* ERROR   = "Error";
    };
}  // namespace api::v2
