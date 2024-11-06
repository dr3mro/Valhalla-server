#pragma once

#include <crow.h>

class RestHelper
{
   public:
    RestHelper()          = default;
    virtual ~RestHelper() = default;
    static void failureResponse(crow::response& res, const std::string& status_message);
    static void errorResponse(crow::response& res, const crow::status& status, const std::string& status_message);
    static void successResponse(crow::response& res, const crow::status& status, const std::string& result);
    static void successResponseJsoned(crow::response& res, const crow::status& status, const std::string& message);
};
