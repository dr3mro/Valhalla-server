#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

using json = jsoncons::json;

class RestHelper
{
   public:
    RestHelper()  = default;
    ~RestHelper() = default;
    // static bool isQuerySuccessful(const json& response);
    // static void buildResponse(json& response_json, short status, const std::string& status_message, const json& results);
    // static void sendResponse(crow::response& res, const int& code, const json& response_json);
    // static void sendErrorResponse(crow::response& res, json& response_json, const std::string& status_message, const std::string& response,
    //                               short status, short code);
    // static int  evaluateQueryResult(json& response_json, const json& query_results_json);
    // static void sendQueryResult(json& response_json, const json& query_results_json, crow::response& res);

    static void failureResponse(crow::response& res, const std::string& status_message);
    static void errorResponse(crow::response& res, const crow::status& status, const std::string& status_message);
    static void successResponse(crow::response& res, const crow::status& status, const std::string& result);
};
