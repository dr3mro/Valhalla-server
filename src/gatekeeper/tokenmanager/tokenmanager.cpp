/**
 * @file tokenmanager.cpp
 * @brief Creates and validates the signed authentication tokens.
 *        (Originally: "Implementation for tokenmanager: Signs a JWT from the
 * client's identity and writes it back to @p clientLoginData.")
 * @details This file builds a JWT from the client's username, id, group and
 *          last-logout time, signs it with the configured secret, and later
 *          decodes and validates tokens presented by clients. The coroutine
 *          form awaits the asynchronous validation path.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Tokens are what a client carries after login to prove who they are on later
 * requests. This file is both the badge printer and the badge checker. Without
 * it, no request could be tied back to a logged-in user.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - JWT (JSON Web Token): https://jwt.io/introduction
 * - HMAC / HS256 signing: https://en.wikipedia.org/wiki/HMAC
 * - Claims (the fields inside a JWT): https://jwt.io/introduction
 * - C++ coroutines: https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include "tokenmanager.hpp"  // The declarations implemented here.

#include <jwt-cpp/jwt.h>  // The JWT creation and decoding API.
// The JSON traits used by jwt-cpp.
#include <jwt-cpp/traits/kazuho-picojson/traits.h>
using TokenManager = api::v2::TokenManager;  // Short alias so definitions can
                                             // omit the namespace.

/**
 * @brief Signs a JWT from the client's identity and writes it back to @p
 * clientLoginData.
 * @return True on success; false when signing throws (reported as a critical
 * message).
 * @see TokenManager::generateToken
 */
// Signs a new token and stores it in clientLoginData;
// true on success.
bool TokenManager::generateToken(
    std::optional<Types::ClientLoginData>& clientLoginData) {
  // Signing or reading optional claims can throw, so guard it.
  try {
    auto tokenManagerParameters =
        getTokenManagerParameters();  // Fetch issuer, type, validity and
                                      // secret from the base class's
                                      // configuration.
    // Create JWT token with payload
    std::optional<std::string>
        token =  // Will hold the signed token if creation succeeds.
        jwt::create<
            jwt::traits::
                kazuho_picojson>()  // Start building a JWT using the chosen
                                    // JSON traits.
            .set_issuer(
                // Record who issued the token.
                std::string(tokenManagerParameters.issuer))
            .set_type(
                // Record the token type.
                std::string(tokenManagerParameters.type))
            .set_subject(clientLoginData->username.value())  // The subject is
                                                             // the client's
                                                             // username.
            .set_id(
                std::to_string(clientLoginData->clientId.value()))  // The token
                                                                    // id is the
                                                                    // numeric
                                                                    // client id
                                                                    // as text.
            .set_issued_at(std::chrono::system_clock::now())        // Stamp the
                                                              // current time as
                                                              // the issue time.
            .set_expires_at(std::chrono::system_clock::now() +
                            std::chrono::minutes{
                                tokenManagerParameters.validity})  // Set
                                                                   // expiry a
            // configured
            // number
            // of
            // minutes
            // from
            // now.
            .set_payload_claim("ip_address",
                               jwt::basic_claim<jwt::traits::kazuho_picojson>(
                                   // Embed the client's IP address as a custom
                                   // claim.
                                   clientLoginData->ip_address.value()))
            .set_payload_claim("llodt",
                               jwt::basic_claim<jwt::traits::kazuho_picojson>(
                                   // Embed the last logout time so a later
                                   // logout can be detected.
                                   clientLoginData->lastLogoutTime.value()))
            .set_payload_claim("group",
                               jwt::basic_claim<jwt::traits::kazuho_picojson>(
                                   clientLoginData->group
                                       .value()))  // Embed the
                                                   // client's
                                                   // group
                                                   // (user or
            // provider).
            .sign(jwt::algorithm::hs256{
                // Sign with the shared secret using HS256 so the token
                // cannot be forged.
                std::string(tokenManagerParameters.secret)});

    // Only store a token that was actually produced.
    if (token.has_value()) {
      clientLoginData->token =
          token.value();  // Write the signed token back for the caller to
                          // send to the client.
      // Token generation succeeded.
      // Runs when a claim is missing or signing fails.
      return true;
    }
  } catch (const std::exception& e) {
    Message::CriticalMessage(
        fmt::format("Error generating token:{}",
                    e.what()));  // Log a serious error with the exception text.
  }
  return false;  // Token generation failed.
}
/**
 * Decodes the token, then validates it with KeeprBase::validateToken.
 * message: Receives the failure reason on a bad or invalid token.
 */
// Decodes and fully validates a token; true only
// when current and genuine.
bool TokenManager::isTokenValid(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string&                           message) {
  // Signing or reading optional claims can throw, so guard it.
  try {
    jwt::decoded_jwt<jwt::traits::kazuho_picojson> decodedToken =
        jwt::decode<jwt::traits::kazuho_picojson>(
            clientLoginData->token.value());  // Parse the token string into
                                              // a structured object.
    // First check that the claims can be read
    // and copied into clientLoginData.
    if (!decodeToken(clientLoginData, message, decodedToken)) {
      message =
          fmt::format("Token is not valid: {}",
                      message);  // Add context to the decode failure reason.
      return false;              // Token generation failed.
    }

    // Token is valid
    // Then verify the signature and
    // account state (the real validity
    // check).
    // Runs when a claim is missing or signing fails.
    return validateToken(clientLoginData, message, decodedToken);
  } catch (const std::exception& e) {
    message =
        fmt::format("Error validating Token:{}",
                    e.what());  // Build an error message from the exception.
    Message::CriticalMessage(message);  // Log the validation error as critical.
  }
  return false;  // Token generation failed.
}

/** @brief Coroutine form of isTokenValid(); see that function for the contract.
 */
// Coroutine form of token validation for async routes.
drogon::Task<bool> TokenManager::isTokenValidCoro(
    std::optional<Types::ClientLoginData>& clientLoginData,
    std::string&                           message) {
  // Signing or reading optional claims can throw, so guard it.
  try {
    jwt::decoded_jwt<jwt::traits::kazuho_picojson> decodedToken =
        jwt::decode<jwt::traits::kazuho_picojson>(
            clientLoginData->token.value());  // Parse the token string into
                                              // a structured object.
    // First check that the claims can be read
    // and copied into clientLoginData.
    if (!decodeToken(clientLoginData, message, decodedToken)) {
      message =
          fmt::format("Token is not valid: {}",
                      message);  // Add context to the decode failure reason.
      co_return false;           // Coroutine return: the token is invalid.
    }

    // Token is valid
    // Await the async
    // validation without
    // blocking, then
    // return its result.
    // A busy database is re-thrown so the caller
    // can retry.
    co_return co_await validateTokenCoro(clientLoginData,
                                         message,
                                         decodedToken);
  } catch (const DatabaseBusy&) {
    // Re-throw the busy error so the caller can retry later.
    // Runs when a claim is missing or signing fails.
    throw;
  } catch (const std::exception& e) {
    message =
        fmt::format("Error validating Token:{}",
                    e.what());  // Build an error message from the exception.
    Message::CriticalMessage(message);  // Log the validation error as critical.
  }
  co_return false;  // Coroutine return: the token is invalid.
}
