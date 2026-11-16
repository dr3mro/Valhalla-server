/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_gatekeeper.cpp
 * @brief Catch2 unit tests and fixtures for gatekeeper. Checks that GateKeeper
 * forwards DoS and authentication checks to its process-wide singletons.
 * @details GateKeeper is a thin facade: isDosAttack delegates to the
 * DOSDetector held in the Store, and isAuthenticationValid delegates to the
 * token verifier, with no database involved. The tests check that a fresh
 * request is allowed, that the DoS verdict is always one of the known status
 * values, and that empty login data or a token signed with the wrong secret is
 * rejected. The detailed threshold and verification logic is covered in the
 * dosdetector, keeprbase and tokenmanager suites.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects gatekeeper.hpp, the single entry point controllers use for
 * security checks. If the facade wired the wrong singleton, or returned a
 * status outside the known set, every protected route could be affected at the
 * same time. These tests keep that thin wiring honest while the exhaustive
 * policy tests cover the details.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / CHECK_FALSE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - std::optional ("a value that may or may not be present"):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides TEST_CASE and the CHECK / CHECK_FALSE macros.
#include <catch2/catch_test_macros.hpp>
// std::optional, the type of the login-data argument that may be empty.
#include <optional>
#include <string>  // std::string, used for the message and token fields.

// DOSDetector::Request and Status, the DoS types the facade passes through.
#include "gatekeeper/dosdetector/dosdetector.hpp"
// The production header under test: api::v2::GateKeeper.
#include "gatekeeper/gatekeeper.hpp"
// makeLoginData / makeToken helpers that build consistent test data.
#include "support/builders.hpp"

using GateKeeper =
    api::v2::GateKeeper;  // Short alias for the facade class under test.

// These tests exercise GateKeeper as a thin facade over process-wide singletons
// (the Store's DOSDetector and token verifier). They deliberately stay on the
// paths that need no database; the underlying policy is covered exhaustively in
// test_dosdetector.cpp and the keeprbase/tokenmanager suites.

// ── isDosAttack (uses singleton DOSDetector from Store) ─────────────────────

TEST_CASE("GateKeeper::isDosAttack allows a fresh request",
    "[gatekeeper][dos]")  // Registers the test that a never-seen request passes
                          // the DoS check.
{
  GateKeeper gk;  // The facade; it finds its dependencies from process-wide
                  // singletons.
  const DOSDetector::Request req{
      // Build a request with designated initializers so each fingerprint
      // field is explicit.
      .ip     = "10.99.88.77",  // Client address; a documentation-style IP that
      // should not be on any list.
      .method = "GET",  // HTTP method, part of the request fingerprint.
      .path   = "/api/v2/users/read",  // Route being requested; also part of
                                       // the fingerprint.
      .header_hash = 0,  // No header hash supplied, so headers do not
      // differentiate this request.
      .body = "fresh-request-body",  // A unique body so this looks like a
                                     // brand-new request.
  };
  CHECK(gk.isDosAttack(req) ==
        DOSDetector::Status::ALLOWED);  // Prove a first-time request is not
                                        // blocked by the detector.
}

TEST_CASE("GateKeeper::isDosAttack returns a valid status for every request",
    "[gatekeeper][dos]")  // Registers the test that the facade only ever
                          // returns known verdicts.
{
  // The singleton DOSDetector uses whatever threshold is in the environment.
  // We only verify the return type is one of the known Status values — the
  // threshold logic is exhaustively tested in test_dosdetector.cpp.
  GateKeeper                 gk;  // The facade under test.
  const DOSDetector::Request req{
      // A second, distinct request so we do not rely on the previous case's
      // state.
      .ip = "10.202.33.44",  // Different client address from the other test.
      .method = "POST",  // Different method exercises a different fingerprint
      // shape.
      .path   = "/api/v2/test",  // Test route, not tied to any real resource.
      .header_hash = 0,          // Header hash left at zero for simplicity.
      .body        = "repeated-gatekeeper-body",  // Distinct body from the
                                                  // fresh-request test.
  };

  const auto status =
      gk.isDosAttack(req);  // Ask the facade for a verdict; the exact value
                            // depends on ambient thresholds.
  CHECK((status == DOSDetector::Status::ALLOWED ||
         status == DOSDetector::Status::RATELIMITED ||
         status == DOSDetector::Status::BANNED));  // Parentheses stop Catch2
                                                   // trying to decompose ||;
                                                   // proves the result is one
                                                   // of the three defined
                                                   // statuses, never garbage.
}

// ── isAuthenticationValid (Store singletons, no DB) ─────────────────────────

TEST_CASE("GateKeeper::isAuthenticationValid rejects an empty ClientLoginData",
    "[gatekeeper][auth]")  // Registers the test for the "no login info at all"
                           // case.
{
  GateKeeper gk;  // The facade under test.
  std::optional<api::v2::Types::ClientLoginData>
              empty;  // An empty optional: there is no login data to validate.
  std::string message;  // Receives a human-readable reason for rejection.
  CHECK_FALSE(gk.isAuthenticationValid(empty,
      message));  // Prove missing login data is rejected rather than crashing
                  // or being accepted.
}

TEST_CASE(
    "GateKeeper::isAuthenticationValid rejects a token with an invalid "
    "signature",
    "[gatekeeper][auth]")  // Registers the test for a correctly shaped but
                           // wrongly signed token.
{
  GateKeeper        gk;  // The facade under test.
  const std::string llodt =
      "2026-01-01 00:00:00 +0000";  // Fixed last-logout time so the token is
                                    // otherwise valid.
  std::optional<api::v2::Types::ClientLoginData> data =
      valhalla_test::makeLoginData(700, "users",
          "127.0.0.1");           // Build consistent login data for client 700.
  data->username       = "user";  // Username that matches the token's subject.
  data->lastLogoutTime = llodt;   // Same last-logout time the token will carry.
  // Sign with the WRONG secret — verification must fail.
  data->token = valhalla_test::makeToken("wrong-secret", "wrong-issuer", "JWS",
      "user", 700, "users", "127.0.0.1",
      llodt);  // Deliberately sign with a different secret and issuer so
               // signature verification cannot succeed.

  std::string message;  // Receives the failure reason.
  CHECK_FALSE(gk.isAuthenticationValid(data,
      message));  // Prove a bad signature is rejected even though every other
                  // field is correct.
}
