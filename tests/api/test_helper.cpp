#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <memory>
#include <string>

#include "api/v2/helper/helper.hpp"

using Helper = api::v2::Helper;

namespace
{
    /// Runs `call` with a callback that captures the response Helper produced.
    drogon::HttpResponsePtr captureResponse(const std::function<void(std::function<void(const drogon::HttpResponsePtr&)>)>& call)
    {
        drogon::HttpResponsePtr captured;
        call([&captured](const drogon::HttpResponsePtr& response) { captured = response; });
        return captured;
    }
}  // namespace

TEST_CASE("Helper::failureResponse answers with 500 and a failure payload", "[api][helper]")
{
    const auto response = captureResponse([](auto&& callback) { Helper::failureResponse("boom", std::move(callback)); });

    REQUIRE(response != nullptr);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k500InternalServerError);
    CHECK(response->getBody().find("boom") != std::string::npos);
    CHECK(response->getBody().find("\"Status\"") != std::string::npos);
    CHECK(response->contentTypeString().find("application/json") != std::string::npos);
}

TEST_CASE("Helper::errorResponse answers with the requested code", "[api][helper]")
{
    const auto response =
        captureResponse([](auto&& callback) { Helper::errorResponse(drogon::HttpStatusCode::k401Unauthorized, "unauthorized", std::move(callback)); });

    REQUIRE(response != nullptr);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k401Unauthorized);
    CHECK(response->getBody().find("unauthorized") != std::string::npos);
}

TEST_CASE("Helper::successResponse answers with 200 and a plain message", "[api][helper]")
{
    const auto response = captureResponse([](auto&& callback) { Helper::successResponse("Logout success.", std::move(callback)); });

    REQUIRE(response != nullptr);
    CHECK(response->getStatusCode() == drogon::HttpStatusCode::k200OK);
    CHECK(response->getBody().find("Logout success.") != std::string::npos);
}

TEST_CASE("Helper::prepare renders the status and message payload", "[api][helper]")
{
    const auto text = Helper::prepare("Failure", "something went wrong");

    CHECK(text.find("\"Message\" : \"something went wrong\"") != std::string::npos);
    CHECK(text.find("\"Status\" : \"Failure\"") != std::string::npos);
}
