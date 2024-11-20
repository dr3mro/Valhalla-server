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
                    // Directly get the authorization header value to avoid multiple dereferences
                    const auto &auth_header = req->getHeader("Authorization");

                    // Check for "Bearer " prefix using a fast substring comparison
                    if (auth_header.empty() || auth_header.size() <= 7 || auth_header[6] != ' ' ||
                        !std::equal(auth_header.begin(), auth_header.begin() + 7, "Bearer "))
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setBody(Helper::stringify(Helper::jsonify("failed to extract token, or not a bearer token")));
                        fcb(resp);
                        return;
                    }

                    // Extract the token
                    TokenManager::LoggedClientInfo clientInfo;
                    clientInfo.token = auth_header.substr(7);  // Efficient token extraction

                    // Get TokenManager object
                    auto tokenManager = Store::getObject<TokenManager>();

                    // Validate token in a single step
                    if (!tokenManager->ValidateToken(clientInfo))
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setBody("Invalid token");
                        fcb(resp);
                        return;
                    }

                    // Token is valid, pass control to the next filter/handler
                    fccb();
                }
            };

        }  // namespace Filters

    }  // namespace v2

}  // namespace api
