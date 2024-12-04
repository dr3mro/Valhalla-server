#include <drogon/drogon.h>

#include "gatekeeper/gatekeeper.hpp"
#include "gatekeeper/types.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
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
                void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&fcb,
                    drogon::FilterChainCallback &&fccb) override
                {
                    // Directly get the authorization header value to avoid
                    // multiple dereferences
                    const auto &auth_header = req->getHeader("Authorization");

                    // Check for "Bearer " prefix using a fast substring
                    // comparison
                    if (auth_header.empty() || auth_header.size() <= 7 || auth_header[6] != ' ' ||
                        !std::equal(auth_header.begin(), auth_header.begin() + 7, "Bearer "))
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setBody(JsonHelper::stringify(
                            JsonHelper::jsonify("failed to extract token, or not a bearer token")));
                        fcb(resp);
                        return;
                    }

                    // Extract the token
                    std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
                    clientLoginData->token                                = auth_header.substr(7);
                    clientLoginData->ip_address                           = req->getPeerAddr().toIp();
                    std::string message;
                    // Validate token
                    if (!gateKeeper->isAuthenticationValid(clientLoginData, message))
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setBody(JsonHelper::stringify(JsonHelper::jsonify(message)));
                        fcb(resp);
                        return;
                    }

                    req->attributes()->insert("clientID", clientLoginData->clientId.value());
                    req->attributes()->insert("clientGroup", clientLoginData->group.value());

                    // Token is valid, pass control to the next filter/handler
                    fccb();
                }

               private:
                std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();
            };

        }  // namespace Filters

    }  // namespace v2

}  // namespace api
