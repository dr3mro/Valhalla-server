#pragma once
#include <drogon/HttpController.h>

#include "utils/resthelper/resthelper.hpp"

namespace api
{
    namespace v2
    {
        class Hello : public drogon::HttpController<Hello>
        {
           public:
            void hello(const drogon::HttpRequestPtr & /*req*/, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setBody(RestHelper::stringify(RestHelper::jsonify("Welcome to the API!")));
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                callback(resp);
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Hello::hello, "", drogon::Get);
            METHOD_LIST_END
        };

    }  // namespace v2
}  // namespace api