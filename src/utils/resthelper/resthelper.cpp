#include "resthelper.hpp"

#include <jsoncons/json.hpp>

#include "fmt/core.h"
using json = jsoncons::json;

void RestHelper::failureResponse(crow::response& res, const std::string& status_message)
{
    res.code = crow::status::INTERNAL_SERVER_ERROR;

    jsoncons::json error_json;
    std::string    response;

    error_json["Status"]  = "Failure";
    error_json["Message"] = status_message;
    error_json.dump_pretty(response);

    res.end(response);
}

void RestHelper::errorResponse(crow::response& res, const crow::status& status, const std::string& status_message)
{
    res.code = status;

    jsoncons::json error_json;
    std::string    response;

    error_json["Status"]  = "Error";
    error_json["Message"] = status_message;
    error_json.dump_pretty(response);

    res.end(response);
}

void RestHelper::successResponse(crow::response& res, const crow::status& status, const std::string& result)
{
    res.code = status;
    res.end(result);
}

void RestHelper::successResponseJsoned(crow::response& res, const crow::status& status, const std::string& message)
{
    std::string results;
    json        response_json;
    response_json["Message"] = message;
    response_json.dump_pretty(results);
    successResponse(res, status, results);
}
