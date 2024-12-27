#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <drogon/drogon_callbacks.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "gatekeeper/gatekeeper.hpp"
#include "gatekeeper/types.hpp"
#include "store/store.hpp"
#include "utils/jsonhelper/jsonhelper.hpp"
namespace api::v2::Filters
{
    class Auth : public drogon::HttpFilter<Auth>
    {
       public:
        Auth() = default;
        void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&fcb, drogon::FilterChainCallback &&fccb) override
        {
            const std::string bearer      = "Bearer ";
            const size_t      bearerMagic = bearer.length();

            // Directly get the authorization header value to avoid
            // multiple dereferences
            const auto &auth_header = req->getHeader("Authorization");

            // Check for "Bearer " prefix using a fast substring
            // comparison
            if (auth_header.empty() || auth_header.size() <= bearerMagic || auth_header[bearerMagic - 1] != ' ' || auth_header.substr(0, bearerMagic) != bearer)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                resp->setBody(JsonHelper::stringify(JsonHelper::jsonify("failed to extract token, or not a bearer token")));
                std::move(fcb)(resp);
                return;
            }

            // Extract the token
            std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
            clientLoginData->token                                = auth_header.substr(bearerMagic);
            clientLoginData->ip_address                           = req->getPeerAddr().toIp();
            std::string message;
            // Validate token
            if (!gateKeeper->isAuthenticationValid(clientLoginData, message))
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                resp->setBody(JsonHelper::stringify(JsonHelper::jsonify(message)));
                std::move(fcb)(resp);
                return;
            }

            req->attributes()->insert("clientID", clientLoginData->clientId.value());
            req->attributes()->insert("clientGroup", clientLoginData->group.value());

            // Token is valid, pass control to the next filter/handler
            std::move(fccb)();
        }

       private:
        std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();
    };
}  // namespace api::v2::Filters
