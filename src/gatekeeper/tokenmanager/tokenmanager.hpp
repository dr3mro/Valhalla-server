/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file tokenmanager.hpp
 * @brief Creates and checks the signed tokens used to authenticate clients.
 *        (Originally: "Issues and validates the HS256 JWTs that authenticate
 * clients.")
 * @details This header declares TokenManager. It builds a JWT (a signed,
 *          self-contained credential) from the client's identity, and checks
 *          tokens that clients send back. A coroutine variant performs the
 *          checks without blocking web request threads.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * After a successful login, the client receives one of these tokens and sends
 * it with later requests. TokenManager is what turns that string back into a
 * trusted identity. Without it, the server could not recognize returning users.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - JWT (JSON Web Token): https://jwt.io/introduction
 * - HMAC / HS256 signing: https://en.wikipedia.org/wiki/HMAC
 * - Coroutines in C++: https://en.cppreference.com/w/cpp/language/coroutines
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

// drogon::Task for the coroutine validation method.
#include "drogon/utils/coroutine.h"
// Base class holding token parameters and validation helpers.
#include "gatekeeper/keeprbase/keeprbase.hpp"
namespace api  // Top-level API namespace.
{
  namespace v2  // Version 2 of the API.
  {
    /**
     * @class TokenManager
     * @brief Issues and validates the HS256 JWTs that authenticate clients.
     * @details Token parameters (issuer, type, validity and secret) come
     * from the Configurator via KeeprBase. Generated tokens carry identity
     * claims and last-logout time so a later logout can be detected.
     * Validation first decodes the token into the ClientLoginData, then
     * verifies it with KeeprBase::validateToken; the coroutine form awaits
     * the async variant.
     * @see KeeprBase
     * In everyday words: this class is like a badge printer and badge
     * checker. It stamps a badge with the user's identity and an expiry,
     * and later checks that a presented badge is genuine and still current.
     */
    class TokenManager : public KeeprBase  // Reuses the base class's token
                                           // settings and validation code.
    {
     public:  // The following methods are the public interface.
      /** @brief Constructs a manager using the Configurator-backed token
       * parameters. */
      TokenManager() = default;  // No extra setup; the base class loads
                                 // the token parameters.
      virtual ~TokenManager() = default;  // Virtual destructor for safe cleanup
                                          // through a base pointer.

      /**
       * @brief Creates a signed JWT from the client's identity and stores
       * it in the data.
       * @param[in,out] clientLoginData Supplies claims and receives the
       * generated token.
       * @return True when a token was produced; false on a
       * signing/configuration error.
       * @note The token embeds the issue/expiry times and last-logout
       * time; signing failures are logged via Message::CriticalMessage.
       * @note Failure paths (missing claims, signing exception) are
       * covered by tests/gatekeeper/test_tokenmanager.cpp.
       */
      bool generateToken(std::optional<Types::ClientLoginData>&
              clientLoginData);  // Signs a new token and stores it in
                                 // clientLoginData; true on success.

      /**
       * @brief Decodes and validates the client's JWT against current
       * account state.
       * @param[in,out] clientLoginData Supplies the token; updated with
       * decoded claims.
       * @param[out] message Receives a human-readable failure reason.
       * @return True only for a decodable, cryptographically valid,
       * current token.
       */
      bool isTokenValid(std::optional<Types::ClientLoginData>& clientLoginData,
          std::string& message);  // Decodes and fully validates a token; true
                                  // only when it is current and genuine.

      /**
       * @brief Coroutine form of isTokenValid(); awaits the async token
       * checks.
       * @param[in,out] clientLoginData Supplies the token; updated with
       * decoded claims.
       * @param[out] message Receives a human-readable failure reason.
       * @return True only for a valid token.
       * @throws DatabaseBusy Propagated from the async validation path.
       */
      drogon::Task<bool> isTokenValidCoro(
          std::optional<Types::ClientLoginData>& clientLoginData,
          std::string& message);  // Coroutine form of token validation,
                                  // used by async routes.
    };
  }  // namespace v2
}  // namespace api
