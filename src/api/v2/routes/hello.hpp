#pragma once
#include <drogon/HttpController.h>

#include "api/v2/basic/common.hpp"
#include "utils/helper/helper.hpp"
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
                resp->setBody(Helper::stringify(Helper::jsonify("Welcome to the API!")));
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                callback(resp);
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Hello::hello, "", drogon::Get, RATELIMIT, ELAPSED);
            METHOD_LIST_END
        };

    }  // namespace v2
}  // namespace api
