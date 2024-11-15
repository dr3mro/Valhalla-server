#pragma once
#include <crow.h>

#include <jsoncons/json.hpp>

class RestHelper
{
   public:
    RestHelper()          = default;
    virtual ~RestHelper() = default;
    static void           failureResponse(crow::response& res, const std::string& message);
    static void           errorResponse(crow::response& res, const crow::status& status, const std::string& message);
    static void           successResponse(crow::response& res, const crow::status& status, const std::string& message);
    static jsoncons::json jsonify(const std::string& message);
    static std::string    stringify(const jsoncons::json& json);

   private:
    static void        reply(crow::response& res, const crow::status& status, const std::string& message);
    static std::string prepare(const std::string& status, const std::string& message);
};
