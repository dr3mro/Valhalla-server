#pragma once
#include <drogon/HttpController.h>

#include "api/v2/basic/common.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
namespace api
{
    namespace v2
    {
        class Hello : public drogon::HttpController<Hello>
        {
           public:
            void hello(const drogon::HttpRequestPtr & /*req*/,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback)
            {
                drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();
                response->setBody(JsonHelper::jsonify("Welcome to the API!").toStyledString());
                response->setStatusCode(drogon::k200OK);
                response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                callback(response);
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Hello::hello, "", drogon::Get, INSECURE);
            METHOD_LIST_END
        };

    }  // namespace v2
}  // namespace api
