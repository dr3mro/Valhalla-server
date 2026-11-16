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

#include "configurator/configurator.hpp"
#include "database/databasebusy.hpp"
#include "drogon/utils/coroutine.h"
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
        void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb, drogon::FilterChainCallback&& fccb) override
        {
            const std::string bearer      = "Bearer ";
            const size_t      bearerMagic = bearer.length();

            // Directly get the authorization header value to avoid
            // multiple dereferences
            const auto& auth_header = req->getHeader("Authorization");

            // Check for "Bearer " prefix using a fast substring
            // comparison
            if (auth_header.empty() || auth_header.size() <= bearerMagic || auth_header[bearerMagic - 1] != ' ' || auth_header.substr(0, bearerMagic) != bearer)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
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
            static const bool asyncDb = Store::cachedObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
            if (asyncDb)
            {
                const size_t                   bearerMagicLocal = bearerMagic;
                std::shared_ptr<GateKeeper>    gateKeeperLocal   = gateKeeper;
                // Move the callbacks into storage owned by the coroutine frame: async_run moves the
                // factory (and its captures) into the coroutine, so they outlive every suspension.
                drogon::async_run([req, bearerMagicLocal, gateKeeperLocal, fcb = std::move(fcb), fccb = std::move(fccb)]() mutable -> drogon::Task<void>
                {
                    std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
                    clientLoginData->token                                = req->getHeader("Authorization").substr(bearerMagicLocal);
                    clientLoginData->ip_address                           = req->getPeerAddr().toIp();
                    std::string             message;
                    bool                    valid = false;
                    drogon::FilterCallback   cb    = std::move(fcb);
                    drogon::FilterChainCallback chain = std::move(fccb);
                    try
                    {
                        valid = co_await gateKeeperLocal->isAuthenticationValidAsync(clientLoginData, message);
                    }
                    catch (const DatabaseBusy& e)
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k503ServiceUnavailable);
                        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                        resp->addHeader("Retry-After", "1");
                        resp->setBody(JsonHelper::stringify(JsonHelper::jsonify(e.what())));
                        std::move(cb)(resp);
                        co_return;
                    }
                    if (!valid)
                    {
                        auto resp = drogon::HttpResponse::newHttpResponse();
                        resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                        resp->setBody(JsonHelper::stringify(JsonHelper::jsonify(message)));
                        std::move(cb)(resp);
                        co_return;
                    }

                    req->attributes()->insert("clientID", clientLoginData->clientId.value());
                    req->attributes()->insert("clientGroup", clientLoginData->group.value());

                    // Token is valid, pass control to the next filter/handler
                    std::move(chain)();
                });
                return;
            }

            bool valid = false;
            try
            {
                valid = gateKeeper->isAuthenticationValid(clientLoginData, message);
            }
            catch (const DatabaseBusy& e)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::HttpStatusCode::k503ServiceUnavailable);
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                resp->addHeader("Retry-After", "1");
                resp->setBody(JsonHelper::stringify(JsonHelper::jsonify(e.what())));
                std::move(fcb)(resp);
                return;
            }
            if (!valid)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
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
