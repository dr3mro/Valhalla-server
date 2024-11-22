#include "helper.hpp"

constexpr std::string FAILURE = "Failure";
constexpr std::string ERROR   = "Error";

void Helper::failureResponse(const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    reply(callback, drogon::k500InternalServerError, prepare(FAILURE, message));
}

void Helper::errorResponse(const drogon::HttpStatusCode& code, const std::string& message,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    reply(callback, code, prepare(ERROR, message));
}

void Helper::successResponse(const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    reply(callback, drogon::k200OK, message);
}

Json::Value Helper::jsonify(const std::string& message)
{
    Json::Value object;
    object["Message"] = message;
    return object;
}
std::string Helper::stringify(const jsoncons::json& json)
{
    std::string json_string;
    json.dump_pretty(json_string);
    return json_string;
}

std::string Helper::stringify(const Json::Value& json) { return json.toStyledString(); }

void Helper::reply(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const drogon::HttpStatusCode& code, const std::string& message)
{
    auto res = drogon::HttpResponse::newHttpResponse();
    res->setStatusCode(code);
    res->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    res->setBody(message);
    callback(res);
}

std::string Helper::prepare(const std::string& status, const std::string& message)
{
    Json::Value object = jsonify(message);
    object["Status"]   = status;
    return object.toStyledString();
}
