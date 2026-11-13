#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "entities/base/case.hpp"
#include "entities/base/entity.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

// Compile-time checks on the entity concepts (STATIC_REQUIRE).
namespace
{
    using ::Provider;
    using ::User;
    using api::v2::Client;
}  // namespace

TEST_CASE("Entity concepts classify the entity hierarchy at compile time", "[utils][concepts]")
{
    STATIC_REQUIRE(Client_t<User>);
    STATIC_REQUIRE(Client_t<Provider>);
    STATIC_REQUIRE(Case_t<::Case>);

    STATIC_REQUIRE_FALSE(Client_t<Entity>);
    STATIC_REQUIRE_FALSE(Service_t<User>);
    STATIC_REQUIRE_FALSE(Case_t<User>);
}

TEST_CASE("Requester exposes id and group with matchers", "[utils][requester]")
{
    const api::v2::Requester requester(42, "providers");

    CHECK(requester.getId() == 42);
    CHECK(requester.getGroup() == "providers");

    CHECK(requester.idMatch(42));
    CHECK_FALSE(requester.idMatch(43));
    CHECK(requester.groupMatch("providers"));
    CHECK_FALSE(requester.groupMatch("users"));

    CHECK(requester.isProvider());
    CHECK_FALSE(requester.isUser());
    CHECK_FALSE(requester.isAdmin());
    CHECK_FALSE(requester.isStaff());
    CHECK_FALSE(requester.isOwner());
    CHECK_FALSE(requester.isSuper());
}

TEST_CASE("Requester can be copied and mutated", "[utils][requester]")
{
    api::v2::Requester original(1, "users");
    const auto         copy = original;
    CHECK(copy.getId() == 1);
    CHECK(copy.getGroup() == "users");

    original.setId(2);
    original.setGroup("admins");
    CHECK(original.getId() == 2);
    CHECK(original.isAdmin());
}

TEST_CASE("Http::Status uses the expected status codes", "[utils][http]")
{
    CHECK(api::v2::Http::Status::OK == static_cast<api::v2::Http::Status>(200));
    CHECK(api::v2::Http::Status::UNAUTHORIZED == static_cast<api::v2::Http::Status>(401));
    CHECK(api::v2::Http::Status::FORBIDDEN == static_cast<api::v2::Http::Status>(403));
    CHECK(api::v2::Http::Status::NOT_FOUND == static_cast<api::v2::Http::Status>(404));
    CHECK(api::v2::Http::Status::BAD_REQUEST == static_cast<api::v2::Http::Status>(400));
    CHECK(api::v2::Http::Status::INTERNAL_SERVER_ERROR == static_cast<api::v2::Http::Status>(500));
}

TEST_CASE("Http::getDescription maps codes to their reason phrases", "[utils][http]")
{
    CHECK(api::v2::Http::getDescription(api::v2::Http::Status::OK) == "OK");
    CHECK(api::v2::Http::getDescription(api::v2::Http::Status::UNAUTHORIZED) == "Unauthorized");
    CHECK(api::v2::Http::getDescription(api::v2::Http::Status::FORBIDDEN) == "Forbidden");
    CHECK(api::v2::Http::getDescription(api::v2::Http::Status::NOT_FOUND) == "Not Found");
    CHECK(api::v2::Http::getDescription(api::v2::Http::Status::INTERNAL_SERVER_ERROR) == "Internal Server Error");

    SECTION("unknown codes report as unknown") { CHECK(api::v2::Http::getDescription(static_cast<api::v2::Http::Status>(599)) == "Unknown Status Code"); }
}

TEST_CASE("Http::Error is a plain aggregate", "[utils][http]")
{
    api::v2::Http::Error error;
    error.code    = api::v2::Http::Status::BAD_REQUEST;
    error.message = "bad input";

    CHECK(error.code == api::v2::Http::Status::BAD_REQUEST);
    CHECK(error.message == "bad input");
}

TEST_CASE("Http::getDescription covers all defined status codes", "[utils][http]")
{
    using api::v2::Http::getDescription;
    using api::v2::Http::Status;

    // 2xx
    CHECK(getDescription(Status::CREATED) == "Created");
    CHECK(getDescription(Status::ACCEPTED) == "Accepted");
    CHECK(getDescription(Status::NO_CONTENT) == "No Content");

    // 3xx
    CHECK(getDescription(Status::MOVED_PERMANENTLY) == "Moved Permanently");
    CHECK(getDescription(Status::TEMPORARY_REDIRECT) == "Temporary Redirect");

    // 4xx
    CHECK(getDescription(Status::CONFLICT) == "Conflict");
    CHECK(getDescription(Status::PAYLOAD_TOO_LARGE) == "Payload Too Large");
    CHECK(getDescription(Status::UNSUPPORTED_MEDIA_TYPE) == "Unsupported Media Type");
    CHECK(getDescription(Status::METHOD_NOT_ALLOWED) == "Method Not Allowed");

    // 5xx
    CHECK(getDescription(Status::NOT_IMPLEMENTED) == "Not Implemented");
    CHECK(getDescription(Status::SERVICE_UNAVAILABLE) == "Service Unavailable");
    CHECK(getDescription(Status::BAD_GATEWAY) == "Bad Gateway");
}

TEST_CASE("Requester isAdmin/isStaff/isOwner/isSuper use the right group names", "[utils][requester]")
{
    CHECK(api::v2::Requester(1, "admins").isAdmin());
    CHECK(api::v2::Requester(1, "staff").isStaff());
    CHECK(api::v2::Requester(1, "owners").isOwner());
    CHECK(api::v2::Requester(1, "Super").isSuper());

    CHECK_FALSE(api::v2::Requester(1, "users").isAdmin());
    CHECK_FALSE(api::v2::Requester(1, "admins").isUser());
    CHECK_FALSE(api::v2::Requester(1, "admins").isProvider());
}
