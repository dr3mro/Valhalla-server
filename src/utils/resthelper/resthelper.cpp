#include "resthelper.hpp"

#include "fmt/core.h"
using json = jsoncons::json;

constexpr std::string FAILURE = "Failure";
constexpr std::string ERROR   = "Error";

void RestHelper::failureResponse(crow::response& res, const std::string& message)
{
    reply(res, crow::status::INTERNAL_SERVER_ERROR, prepare(FAILURE, message));
}

void RestHelper::errorResponse(crow::response& res, const crow::status& status, const std::string& message)
{
    reply(res, status, prepare(ERROR, message));
}

void RestHelper::successResponse(crow::response& res, const crow::status& status, const std::string& message) { reply(res, status, message); }

jsoncons::json RestHelper::jsonify(const std::string& message)
{
    jsoncons::json object;
    object["Message"] = message;
    return object;
}

std::string RestHelper::stringify(const jsoncons::json& json)
{
    std::string reply;
    json.dump_pretty(reply);
    return reply;
}

void RestHelper::reply(crow::response& res, const crow::status& status, const std::string& message)
{
    res.code = status;
    res.add_header("Content-Type", "application/json");
    res.end(message);
}

std::string RestHelper::prepare(const std::string& status, const std::string& message)
{
    jsoncons::json object = jsonify(message);
    object["Status"]      = status;
    return stringify(object);
}
