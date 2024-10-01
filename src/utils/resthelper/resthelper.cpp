#include "resthelper.hpp"

#include "fmt/core.h"

void RestHelper::failureResponse(crow::response& res, const std::string& status_message)
{
    res.code = crow::status::INTERNAL_SERVER_ERROR;
    res.add_header("status", "failure");

    jsoncons::json error_json;
    std::string    response;

    error_json["message"] = status_message;
    error_json.dump_pretty(response);

    res.end(response);
}

void RestHelper::errorResponse(crow::response& res, const crow::status& status, const std::string& status_message)
{
    res.code = status;
    res.add_header("status", "error");

    jsoncons::json error_json;
    std::string    response;

    error_json["message"] = status_message;
    error_json.dump_pretty(response);

    res.end(response);
}

void RestHelper::successResponse(crow::response& res, const crow::status& status, const std::string& result)
{
    res.code = status;
    res.add_header("status", "success");
    res.end(result);
}