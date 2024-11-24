#include "helper.hpp"

#include "utils/jsonhelper/jsonhelper.hpp"

using namespace api::v2;
constexpr char FAILURE[] = "Failure";
constexpr char ERROR[]   = "Error";

void Helper::failureResponse(const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    reply(std::move(callback), drogon::k500InternalServerError, prepare(FAILURE, message));
}

void Helper::errorResponse(const drogon::HttpStatusCode& code, const std::string& message,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    reply(std::move(callback), code, prepare(ERROR, message));
}

void Helper::successResponse(const std::string& message, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    reply(std::move(callback), drogon::k200OK, message);
}

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
    Json::Value object = JsonHelper::jsonify(message);
    object["Status"]   = status;
    return object.toStyledString();
}
