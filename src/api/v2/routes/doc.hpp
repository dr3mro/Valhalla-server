#pragma once
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <functional>
#include <string>
#include <utility>

#include "api/v2/basic/common.hpp"
#include "api_html.hpp"  // generated at configure time from docs/api.html (see tools/cmake/embed_html.cmake)

namespace api::v2
{
    // GET /api/v2/doc  ->  the Swagger UI page describing the API. The page is compiled into the binary.
    class Doc : public drogon::HttpController<Doc>
    {
       public:
        void doc(const drogon::HttpRequestPtr & /*req*/, std::function<void(const drogon::HttpResponsePtr &)> &&callback)  // NOLINT
        {
            drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();

            response->setBody(std::string(docs::kApiHtml));
            response->setStatusCode(drogon::k200OK);
            response->setContentTypeCode(drogon::CT_TEXT_HTML);
            std::move(callback)(response);
        }

        METHOD_LIST_BEGIN
        METHOD_ADD(Doc::doc, "", drogon::Get, INSECURE);
        METHOD_LIST_END
    };

}  // namespace api::v2
