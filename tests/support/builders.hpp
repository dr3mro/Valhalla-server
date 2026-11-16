/**
 * @file builders.hpp
 * @brief Small helpers that build test data: login records, caller identity,
 * and signed JWT tokens.
 * @details These builders are shared by many tests. They create a fully
 * filled-in login record, a "who is calling" value for controller methods, and
 * signed tokens — including a deliberately corrupted token for negative tests.
 * Keeping them in one place means every test starts from the same known-good
 * values, so a failure points at the code under test rather than at the test
 * data.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * These helpers feed the authentication and gatekeeper paths — the code that
 * decides who a caller is and whether their token can be trusted. Without them,
 * each test would hand-build its own data and tiny differences (for example
 * leaving a field uninitialised) would make tests flaky or hide real bugs in
 * token verification.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - RAII (a guard object that cleans up in its destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - JWTs (signed tokens used to prove identity): https://jwt.io/introduction
 * - jwt-cpp library: https://github.com/Thalhammer/jwt-cpp
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Tell the compiler to include this header at most once per
              // translation unit.

#include <jwt-cpp/jwt.h>  // Library used to create and sign JSON Web Tokens.
// Picks the picojson JSON backend that jwt-cpp should use.
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <chrono>  // Time points and durations, used to set a token's expiry.
// Fixed-width integers such as uint64_t, so ids have a known size.
#include <cstdint>
#include <optional>  // std::optional support for values that may be absent.
#include <string>    // std::string, used for all the text fields here.

// Defines ClientLoginData, the login record this helper fills in.
#include "gatekeeper/types.hpp"
// Defines Requester, the caller-identity value built below.
#include "utils/global/requester.hpp"

namespace valhalla_test  // A dedicated namespace keeps test helpers from
                         // clashing with production names.
{
  using api::v2::Types::ClientLoginData;  // Shorthand so tests can write
                                          // ClientLoginData directly.

  /**
   * ClientLoginData with every field initialised (the production struct
   * leaves `is_active` / `passwordMatch` uninitialised).
   * @brief Builds a login record with all fields set to known values.
   * @param clientId Numeric id of the caller; defaults to 1.
   * @param group Permission group; defaults to "users".
   * @param ip Caller's IP address; defaults to the local loopback address.
   * @return A ready-to-use login record for authentication tests.
   */
  inline ClientLoginData makeLoginData(
      const uint64_t     clientId = 1,
      const std::string& group    = "users",
      const std::string& ip =
          "127.0.0.1")  // Defaults describe a typical "normal user" caller.
  {
    ClientLoginData
        data{};  // Value-initialise first so nothing is left random.
    data.clientId =
        clientId;        // The caller's id, used later to find their account.
    data.group = group;  // The permission group (e.g. "users" or "providers").
    data.username =
        "testuser";  // A fixed name so assertions know what to expect.
    data.ip_address =
        ip;  // Where the request came from; some checks read this.
    data.is_active = true;  // Mark the account active so validation succeeds.
    data.passwordMatch = false;  // Pretend the password was not checked
                                 // here (no real hashing).
    data.expireTime =
        std::chrono::system_clock::now().time_since_epoch() +
        std::chrono::hours{1};  // Expiry one hour from now, stored as ticks
                                // since the epoch.
    return data;  // Give the fully built record back to the caller.
  }

  /**
   * Builds the request-context value passed to controller methods, so tests
   * can supply a known caller identity without constructing it inline.
   * @brief Wraps the constructor for Requester in a named helper.
   */
  inline api::v2::Requester makeRequester(const uint64_t     id,
                                          const std::string& group)
  {
    return api::v2::Requester(id, group);
  }  // Returns the caller id and group bundled into one value.

  /**
   * Issues a JWT the same way TokenManager does (picojson traits, HS256).
   * Not verified against the production secret unless `secret` matches.
   * @brief Creates a signed token containing the claims the server expects.
   * @details Each claim is a piece of identity data written into the token so
   * that the server can read it back after verifying the signature.
   */
  inline std::string makeToken(
      const std::string&         secret,
      const std::string&         issuer,
      const std::string&         type,
      const std::string&         username,
      uint64_t                   clientId,
      const std::string&         group,
      const std::string&         ip,
      const std::string&         lastLogoutTime,
      const std::chrono::minutes validity = std::chrono::minutes{
          60})  // `validity` defaults to one hour, matching normal tokens.
  {
    return jwt::create<
               jwt::traits::
                   kazuho_picojson>()  // Start a new token using the picojson
                                       // backend.
        .set_issuer(issuer)            // Record who issued the
                                       // token.
        .set_type(type)                // Record the token
                                       // type (e.g. "JWS").
        .set_subject(username)         // Store the username the
                                       // token is about.
        .set_id(std::to_string(clientId))  // Store the numeric
                                           // client id as text.
        .set_issued_at(
            std::chrono::system_clock::now())  // Stamp the current
                                               // time as "issued at".
        .set_expires_at(std::chrono::system_clock::now() +
                        validity)  // Set expiry so old
                                   // tokens are rejected.
        .set_payload_claim("ip_address",
                           jwt::basic_claim<jwt::traits::kazuho_picojson>(
                               ip))  // Extra claim: the
                                     // caller's IP.
        .set_payload_claim("llodt",
                           jwt::basic_claim<jwt::traits::kazuho_picojson>(
                               lastLogoutTime))  // Extra
                                                 // claim: last
                                                 // logout
                                                 // time, used
                                                 // to
                                                 // invalidate
                                                 // old tokens.
        .set_payload_claim("group",
                           jwt::basic_claim<jwt::traits::kazuho_picojson>(
                               group))         // Extra claim: the
                                               // caller's permission
                                               // group.
        .sign(jwt::algorithm::hs256{secret});  // Sign with HS256 and
                                               // the given secret so
                                               // tampering can be
                                               // detected.
  }

  /**
   * A token signed with the *wrong* key (for tampering/verification tests).
   * @brief Corrupts a token's signature so verification must fail.
   */
  inline std::string makeTamperedToken(const std::string& token)
  {
    // Flip a character in the middle of the signature (the third JWT
    // segment, after the last '.'). We deliberately avoid the last
    // character: for a 32-byte HMAC-SHA256 output the signature is 43
    // base64url chars, and the last char's bottom 2 bits are padding zeros
    // — 'A' and 'B' decode to the same bytes, so flipping them does not
    // actually change the signature.
    std::string tampered =
        token;  // Work on a copy so the original token is untouched.
    const std::size_t dot =
        tampered.rfind('.');  // Find the last dot: it separates the
                              // signature from the rest.
    if (dot == std::string::npos ||
        dot + 4 >= tampered.size())  // Bail out if the token is malformed
                                     // or too short to edit safely.
      return tampered;            // Return the unchanged copy rather than risk
                                  // reading past the end.
    char& c = tampered[dot + 4];  // Grab a signature character safely away
                                  // from the padding at the end.
    c       = (c == 'A') ? 'Z' : 'A';  // Change it to a different base64url
                                       // character, invalidating the signature.
    return tampered;  // Hand back the corrupted token for negative tests.
  }
}  // namespace valhalla_test
