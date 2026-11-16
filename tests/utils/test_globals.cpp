/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_globals.cpp
 * @brief Catch2 unit tests for the shared globals: entity concepts, Requester
 * and HTTP status types.
 * @details This file pins down three small but widely used utilities. It checks
 * that the C++20 entity concepts accept the right domain classes at compile
 * time. It checks that Requester stores an id and a group and answers
 * membership questions correctly. It also checks that HTTP status codes keep
 * their standard numbers and map to the expected human-readable reason phrases.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Requester is the caller identity the gatekeeper and permission manager rely
 * on; if a group check silently changed, permissions could be granted to the
 * wrong people. The Status enum and getDescription are the shared vocabulary of
 * every HTTP response, so a wrong number or phrase would break clients. The
 * entity concepts are a compile-time gate on the generic controllers, and this
 * test documents exactly which types are allowed through that gate.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertion macros (CHECK, CHECK_FALSE, STATIC_REQUIRE):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - C++20 concepts / constraints:
 * https://en.cppreference.com/w/cpp/language/constraints
 * - std::is_base_of: https://en.cppreference.com/w/cpp/types/is_base_of
 * - Fixed-width integers such as uint64_t:
 * https://en.cppreference.com/w/cpp/types/integer
 * - Scoped/unscoped enums: https://en.cppreference.com/w/cpp/language/enum
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Brings in TEST_CASE, SECTION, CHECK, CHECK_FALSE and STATIC_REQUIRE.
#include <catch2/catch_test_macros.hpp>
// Fixed-width integer types such as the uint64_t used for Requester's id.
#include <cstdint>
// std::string, used for the group name and the HTTP reason phrases.
#include <string>
// std::is_base_of_v, the machinery behind the entity concepts.
#include <type_traits>
// The standard dynamic-array header (no vector is used directly in this file).
#include <vector>

// Full definition of Case, needed for the Case_t<::Case> check.
#include "entities/base/case.hpp"
// Definition of the generic Entity base used in a negative concept check.
#include "entities/base/entity.hpp"
// Provider entity used in one of the Client_t checks.
#include "entities/people/provider.hpp"
// User entity used in one of the Client_t checks.
#include "entities/people/user.hpp"
// The Client_t / Service_t / Case_t concepts under test.
#include "utils/global/concepts.hpp"
// Http::Status, getDescription and Http::Error under test.
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"  // The Requester value object under test.

// Compile-time checks on the entity concepts (STATIC_REQUIRE).
namespace
{
  using ::Provider;       // Bring the global Provider class into this anonymous
                          // namespace for shorter names below.
  using ::User;           // Same for User, so Client_t<User> reads cleanly.
  using api::v2::Client;  // Same for the version-2 Client class used in the
                          // concept checks.
}  // namespace

// These checks happen during compilation: an accepted type lets a generic
// controller instantiate, a rejected one does not.
TEST_CASE("Entity concepts classify the entity hierarchy at compile time",
          "[utils][concepts]")
{
  STATIC_REQUIRE(
      Client_t<User>);  // Compile-time assert: User satisfies Client_t, so
                        // controllers accept it as a client.
  STATIC_REQUIRE(
      Client_t<Provider>);  // Compile-time assert: Provider also counts as a
                            // Client (it derives from it).
  STATIC_REQUIRE(Case_t< ::Case>);  // Compile-time assert: the base Case
                                    // itself satisfies Case_t.

  STATIC_REQUIRE_FALSE(Client_t<Entity>);  // The generic Entity base is NOT a
                                           // Client; the gate must reject it.
  STATIC_REQUIRE_FALSE(Service_t<User>);   // A User is not a Service either;
                                           // these concepts must not overlap.
  STATIC_REQUIRE_FALSE(Case_t<User>);  // A User is not a Case, so this check
                                       // must also fail at compile time.
}

// Requester carries an id plus a group and exposes the matching helpers used by
// permission rules.
TEST_CASE("Requester exposes id and group with matchers", "[utils][requester]")
{
  const api::v2::Requester
      requester(42,
                "providers");  // Make an immutable caller with numeric id 42 in
                               // the "providers" group.

  CHECK(requester.getId() ==
        42);  // The getter must return exactly the id given to the constructor.
  CHECK(requester.getGroup() == "providers");  // The getter must return exactly
                                               // the group text we supplied.

  CHECK(requester.idMatch(42));  // Matching against the same id must be true.
  CHECK_FALSE(requester.idMatch(
      43));  // A different id must be false, so ownership checks stay strict.
  CHECK(requester.groupMatch("providers"));  // The exact group name matches.
  CHECK_FALSE(
      requester.groupMatch("users"));  // A different group name must not match.

  CHECK(requester.isProvider());  // The "providers" convenience helper must
                                  // agree with groupMatch.
  CHECK_FALSE(
      requester
          .isUser());  // The other role helpers must say no for this caller.
  CHECK_FALSE(requester.isAdmin());  // "providers" is not "admins".
  CHECK_FALSE(requester.isStaff());  // "providers" is not "staff".
  CHECK_FALSE(requester.isOwner());  // "providers" is not "owners".
  CHECK_FALSE(
      requester.isSuper());  // "providers" is not "Super" (case matters).
}

// A Requester is a cheap value type: copying is independent and setters mutate
// only the original.
TEST_CASE("Requester can be copied and mutated", "[utils][requester]")
{
  api::v2::Requester
      original(1, "users");    // A mutable caller object we will change later.
  const auto copy = original;  // Take a snapshot; changing original must not
                               // change this copy.
  CHECK(copy.getId() == 1);    // The copy starts with the same id.
  CHECK(copy.getGroup() ==
        "users");  // The copy starts with the same group text.

  original.setId(2);  // Change the id to prove the setter really writes.
  original.setGroup("admins");   // Change the group to a new role.
  CHECK(original.getId() == 2);  // The new id is visible through the getter.
  CHECK(original
            .isAdmin());  // The role helper now recognises the new group name.
}

// The Status enum mirrors standard HTTP numbers; clients depend on those exact
// values.
TEST_CASE("Http::Status uses the expected status codes", "[utils][http]")
{
  // static_cast turns the plain number into the enum type so the two can be
  // compared.
  CHECK(api::v2::Http::Status::OK ==
        static_cast<api::v2::Http::Status>(200));  // 200 = success.
  CHECK(api::v2::Http::Status::UNAUTHORIZED ==
        static_cast<api::v2::Http::Status>(401));  // 401 = login required.
  CHECK(api::v2::Http::Status::FORBIDDEN ==
        static_cast<api::v2::Http::Status>(403));  // 403 = refused.
  CHECK(api::v2::Http::Status::NOT_FOUND ==
        static_cast<api::v2::Http::Status>(404));  // 404 = missing resource.
  CHECK(api::v2::Http::Status::BAD_REQUEST ==
        static_cast<api::v2::Http::Status>(400));  // 400 = bad input.
  CHECK(api::v2::Http::Status::INTERNAL_SERVER_ERROR ==
        static_cast<api::v2::Http::Status>(500));  // 500 = unexpected failure.
}

// Each known code maps to its standard reason phrase; unknown codes fall back
// safely instead of crashing.
TEST_CASE("Http::getDescription maps codes to their reason phrases",
          "[utils][http]")
{
  CHECK(api::v2::Http::getDescription(api::v2::Http::Status::OK) ==
        "OK");  // 200 -> "OK".
  CHECK(api::v2::Http::getDescription(api::v2::Http::Status::UNAUTHORIZED) ==
        "Unauthorized");  // 401 -> "Unauthorized".
  CHECK(api::v2::Http::getDescription(api::v2::Http::Status::FORBIDDEN) ==
        "Forbidden");  // 403 -> "Forbidden".
  CHECK(api::v2::Http::getDescription(api::v2::Http::Status::NOT_FOUND) ==
        "Not Found");  // 404 -> "Not Found".
  CHECK(api::v2::Http::getDescription(
            api::v2::Http::Status::INTERNAL_SERVER_ERROR) ==
        "Internal Server Error");  // 500 -> "Internal Server Error".

  // SECTION creates an isolated branch: Catch2 reruns the parent test case
  // for it.
  SECTION("unknown codes report as unknown")
  {
    CHECK(api::v2::Http::getDescription(static_cast<api::v2::Http::Status>(
              599)) == "Unknown Status Code");
  }  // 599 is not in the table, so the documented fallback phrase must come
     // back.
}

// Error is a plain struct: its fields are public and are set directly, with no
// constructor call needed.
TEST_CASE("Http::Error is a plain aggregate", "[utils][http]")
{
  api::v2::Http::Error error;  // Default-construct the simple payload holder.
  error.code = api::v2::Http::Status::BAD_REQUEST;  // Store a numeric problem
                                                    // code (400) for machines.
  error.message = "bad input";  // Store the human explanation next to it.

  CHECK(error.code ==
        api::v2::Http::Status::BAD_REQUEST);  // The code must survive being
                                              // written and read back.
  CHECK(error.message == "bad input");  // The message must survive as well.
}

// A broader sweep: every status family the server actually returns must have
// the right phrase.
TEST_CASE("Http::getDescription covers all defined status codes",
          "[utils][http]")
{
  using api::v2::Http::getDescription;  // Shorthand so the checks below read
                                        // plainly.
  using api::v2::Http::Status;          // Shorthand for the enum type.

  // 2xx
  CHECK(getDescription(Status::CREATED) ==
        "Created");  // 201: a new resource was made.
  CHECK(getDescription(Status::ACCEPTED) ==
        "Accepted");  // 202: request accepted, work not finished.
  CHECK(getDescription(Status::NO_CONTENT) ==
        "No Content");  // 204: success with no body.

  // 3xx
  CHECK(getDescription(Status::MOVED_PERMANENTLY) ==
        "Moved Permanently");  // 301: new permanent address.
  CHECK(getDescription(Status::TEMPORARY_REDIRECT) ==
        "Temporary Redirect");  // 307: redirect for now.

  // 4xx
  CHECK(getDescription(Status::CONFLICT) ==
        "Conflict");  // 409: request conflicts with current state.
  CHECK(getDescription(Status::PAYLOAD_TOO_LARGE) ==
        "Payload Too Large");  // 413: body is too big.
  CHECK(getDescription(Status::UNSUPPORTED_MEDIA_TYPE) ==
        "Unsupported Media Type");  // 415: media type not allowed.
  CHECK(getDescription(Status::METHOD_NOT_ALLOWED) ==
        "Method Not Allowed");  // 405: method not allowed here.

  // 5xx
  CHECK(getDescription(Status::NOT_IMPLEMENTED) ==
        "Not Implemented");  // 501: feature not supported yet.
  CHECK(getDescription(Status::SERVICE_UNAVAILABLE) ==
        "Service Unavailable");  // 503: temporarily down.
  CHECK(getDescription(Status::BAD_GATEWAY) ==
        "Bad Gateway");  // 502: upstream sent a bad reply.
}

// The isX helpers compare hard-coded strings, so exact spelling (and capital S
// for Super) matters.
TEST_CASE("Requester isAdmin/isStaff/isOwner/isSuper use the right group names",
          "[utils][requester]")
{
  CHECK(api::v2::Requester(1, "admins")
            .isAdmin());  // The literal "admins" is what unlocks isAdmin.
  CHECK(api::v2::Requester(1, "staff")
            .isStaff());  // The literal "staff" unlocks isStaff.
  CHECK(api::v2::Requester(1, "owners")
            .isOwner());  // The literal "owners" unlocks isOwner.
  CHECK(api::v2::Requester(1, "Super")
            .isSuper());  // Only the capitalised "Super" unlocks isSuper.

  CHECK_FALSE(api::v2::Requester(1, "users")
                  .isAdmin());  // A normal user is not an admin.
  CHECK_FALSE(api::v2::Requester(1, "admins")
                  .isUser());  // An admin is not in the "users" group.
  CHECK_FALSE(api::v2::Requester(1, "admins")
                  .isProvider());  // An admin is not a provider either.
}
