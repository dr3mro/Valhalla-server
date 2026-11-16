// Regression test for the async auth filter use-after-free.
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/utils/coroutine.h>

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "api/v2/filters/auth.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"

using AuthFilter = api::v2::Filters::Auth;

namespace
{
    class FakeGateKeeper : public api::v2::GateKeeper
    {
       public:
        drogon::Task<bool> isAuthenticationValidAsync(std::optional<api::v2::Types::ClientLoginData>& cd, std::string& /*m*/) override
        {
            cd->clientId = 1000;
            cd->group    = "providers";
            co_return true;
        }
    };
}  // namespace

TEST_CASE("Auth filter async path invokes the chain callback", "[api][auth][async]")
{
    auto fake = std::make_shared<FakeGateKeeper>();
    auto orig = Store::getObject<api::v2::GateKeeper>();
    Store::replaceObject<api::v2::GateKeeper>(fake);

    auto request = drogon::HttpRequest::newHttpRequest();
    request->addHeader("Authorization", "Bearer sometoken");

    AuthFilter filter;
    std::atomic<bool> chainCalled{false};
    std::atomic<int>  cbCount{0};
    filter.doFilter(
        request,
        [&cbCount](const drogon::HttpResponsePtr&) { ++cbCount; },
        [&chainCalled] { chainCalled = true; });

    for (int i = 0; i < 100 && !chainCalled.load() && cbCount.load() == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Store::replaceObject<api::v2::GateKeeper>(orig);

    CHECK(chainCalled.load());
}
