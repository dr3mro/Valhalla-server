// A pool timeout (DatabaseBusy) must reach the client as 503 with Retry-After, not as a generic failure.
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>
#include <string>

#include "api/v2/helper/helper.hpp"
#include "controllers/clientcontroller/clientcontroller.hpp"
#include "database/databasebusy.hpp"
#include "entities/people/user.hpp"
#include "support/fake_db.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/errorstatus.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

namespace Http = api::v2::Http;
using api::v2::Requester;

TEST_CASE("statusForException maps DatabaseBusy to 503 and everything else to 500", "[database][busy]")
{
    CHECK(Http::statusForException(DatabaseBusy()) == Http::Status::SERVICE_UNAVAILABLE);
    CHECK(Http::statusForException(std::runtime_error("boom")) == Http::Status::INTERNAL_SERVER_ERROR);
}

TEST_CASE("A 503 response carries Retry-After", "[database][busy]")
{
    drogon::HttpResponsePtr response;
    api::v2::Helper::errorResponse(drogon::k503ServiceUnavailable, "busy", [&response](const drogon::HttpResponsePtr& r) { response = r; });

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k503ServiceUnavailable);
    CHECK(response->getHeader("Retry-After") == "1");
}

TEST_CASE("Other error responses carry no Retry-After", "[database][busy]")
{
    drogon::HttpResponsePtr response;
    api::v2::Helper::failureResponse("boom", [&response](const drogon::HttpResponsePtr& r) { response = r; });

    REQUIRE(response != nullptr);
    CHECK(response->statusCode() == drogon::k500InternalServerError);
    CHECK(response->getHeader("Retry-After").empty());
}

TEST_CASE("A controller answers 503 when the database is busy", "[controller][client][busy]")
{
    auto fake  = std::make_shared<FakeDatabaseController>();
    fake->busy = true;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    int                    code = -1;
    std::string            body;

    ctrl.Activate([&](int c, const std::string& b)
        {
            code = c;
            body = b;
        },
        Requester(42, "users"), 42u);

    CHECK(code == Http::Status::SERVICE_UNAVAILABLE);
    CHECK(body.find("busy") != std::string::npos);
}

TEST_CASE("A non-busy database failure is still a 400/500, never 503", "[controller][client][busy]")
{
    auto fake          = std::make_shared<FakeDatabaseController>();
    fake->query_result = std::nullopt;

    FakeDbGuard            guard(fake);
    ClientController<User> ctrl;
    int                    code = -1;

    ctrl.Activate([&](int c, const std::string&) { code = c; }, Requester(42, "users"), 42u);

    CHECK(code != Http::Status::SERVICE_UNAVAILABLE);
}
