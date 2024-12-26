#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <drogon/drogon_callbacks.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "gatekeeper/dosdetector/dosdetector.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"
namespace api::v2::Filters
{
    class RateLimit : public drogon::HttpFilter<RateLimit>
    {
       public:
        RateLimit() = default;
        void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&fcb, drogon::FilterChainCallback &&fccb) override
        {
            std::shared_ptr<GateKeeper> gatekeeper = Store::getObject<GateKeeper>();

            DOSDetector::Request request = {.ip = req->peerAddr().toIp(),
                .method                         = req->methodString(),
                .path                           = req->path(),
                .headers = std::unordered_map<std::string, std::string, std::hash<std::string>>(req->getHeaders().begin(), req->getHeaders().end()),
                .body    = req->body()};

            DOSDetector::Status status = gatekeeper->isDosAttack(request);

            switch (status)
            {
                case DOSDetector::Status::ALLOWED:
                case DOSDetector::Status::WHITELISTED:
                    fccb();
                    break;
                case DOSDetector::Status::BLACKLISTED:
                    reply("IP is blacklisted.", drogon::k403Forbidden, std::move(fcb));
                    break;
                case DOSDetector::Status::RATELIMITED:
                    reply("IP is blacklisted.", drogon::k429TooManyRequests, std::move(fcb));
                    break;
                case DOSDetector::Status::BANNED:
                    reply("IP is banned.", drogon::k503ServiceUnavailable, std::move(fcb));
                    break;
                case DOSDetector::Status::ERROR:
                    reply("Server error.", drogon::k500InternalServerError, std::move(fcb));
                    break;
                default:
                    fccb();
                    break;
            }
        }

       private:
        std::shared_ptr<DOSDetector> dos_detector = Store::getObject<DOSDetector>();
        void                         reply(const std::string &&msg, drogon::HttpStatusCode code, const drogon::FilterCallback &&cb)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(msg);
            resp->setStatusCode(code);
            cb(resp);
        }
    };
}  // namespace api::v2::Filters
