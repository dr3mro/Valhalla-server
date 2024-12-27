#pragma once
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <functional>
#include <utility>

#include "api/v2/basic/common.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
namespace api::v2
{
    class Hello : public drogon::HttpController<Hello>
    {
       public:
        void hello(const drogon::HttpRequestPtr & /*req*/, std::function<void(const drogon::HttpResponsePtr &)> &&callback)  // NOLINT
        {
            drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();
            response->setBody(JsonHelper::jsonify("Welcome to the API!").toStyledString());
            response->setStatusCode(drogon::k200OK);
            response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            std::move(callback)(response);
        }

        METHOD_LIST_BEGIN
        METHOD_ADD(Hello::hello, "", drogon::Get, INSECURE);
        METHOD_LIST_END
    };

}  // namespace api::v2
