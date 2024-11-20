#include <drogon/drogon.h>

#include "utils/helper/helper.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"
namespace api
{
    namespace v2
    {
        namespace Filters
        {
            class Auth : public drogon::HttpFilter<Auth>
            {
               public:
                Auth() = default;
                void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&fcb, drogon::FilterChainCallback &&fccb) override
                {
                    std::optional<std::string> auth_header = req->getHeader("Authorization");

                    if (!auth_header || !auth_header.value().starts_with("Bearer"))
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setBody(Helper::stringify(Helper::jsonify("failed to extract token, or not a bearer token")));
                        fcb(resp);
                        return;
                    }

                    clientInfo.token  = auth_header.value().substr(7);
                    auto tokenManager = Store::getObject<TokenManager>();

                    if (!tokenManager->ValidateToken(clientInfo))
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setBody("Invalid token");
                        fcb(resp);
                        return;
                    }
                    fccb();
                }

                TokenManager::LoggedClientInfo getClientInfo() { return clientInfo; }

               private:
                TokenManager::LoggedClientInfo clientInfo;
            };
        }  // namespace Filters

    }  // namespace v2

}  // namespace api
