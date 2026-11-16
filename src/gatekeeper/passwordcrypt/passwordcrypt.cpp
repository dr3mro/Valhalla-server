/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file passwordcrypt.cpp
 * @brief The implementation of the password hashing and checking wrapper.
 *        (Originally: "Implementation for passwordcrypt: Ensures libsodium is
 * initialized before any hashing or verification.")
 * @details This file sets up libsodium once, turns a plain password into a
 *          self-describing Argon2id hash, and checks plain passwords against
 *          stored hashes. It understands both the new Argon2id format and the
 *          older scrypt format so old accounts are not locked out.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the actual password cryptography happens. The session manager
 * calls these functions (through PasswordWorker) during login and logout. If
 * this code were missing, the server could not safely store passwords or prove
 * that a supplied password is correct, so authentication would be impossible.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - libsodium password hashing API: https://doc.libsodium.org/password_hashing/
 * - Argon2id explained: https://en.wikipedia.org/wiki/Argon2
 * - scrypt explained: https://en.wikipedia.org/wiki/Scrypt
 * - OWASP password storage advice:
 * https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include "passwordcrypt.hpp"  // The declarations this file implements.

#include <sodium/crypto_pwhash.h>  // Argon2 hashing and verification functions.
// Legacy scrypt verification, for old accounts.
#include <sodium/crypto_pwhash_scryptsalsa208sha256.h>

#include <cstddef>  // std::size_t: an unsigned type used for the memory limit.
// std::optional/std::nullopt for a hash that may fail to be produced.
#include <optional>
#include <string>  // std::string for passwords and encoded hashes.

#include "sodium/core.h"  // sodium_init(), the one-time library setup call.
// Message::CriticalMessage() for reporting failures.
#include "utils/message/message.hpp"

namespace
{
  // Argon2id at OWASP's minimum recommended cost (19 MiB of memory, 2 passes,
  // 1 lane; libsodium always uses one lane). "Cost" means how hard the recipe
  // makes a guessing attack: more memory and more passes make hashes slower
  // to attack but also slower for us to compute. The work runs on the
  // PasswordWorker threads, never on a request thread, so this cost cannot
  // stall the web server.
  constexpr unsigned long long ARGON2ID_OPSLIMIT =
      2;  // Number of passes over memory; small but meets OWASP's minimum.
  constexpr std::size_t ARGON2ID_MEMLIMIT =
      19UL * 1024UL * 1024UL;  // 19 MiB of RAM per hash; 19*1024*1024 bytes.

  constexpr const char* ARGON2_PREFIX =
      "$argon2";  // $argon2id$... label at the start of new hashes (and
                  // $argon2i$... if imported).
  constexpr const char* SCRYPT_PREFIX =
      "$7$";  // Label of libsodium's scrypt format, written by older versions
              // of this server.
}  // namespace

/**
 * @brief Ensures libsodium is initialized before any hashing or verification.
 * @details A failed initialization is fatal for password operations and is
 * reported through Message::CriticalMessage; construction still succeeds. In
 * everyday words, libsodium needs a one-time "wake up" call before it can be
 * used; this constructor makes that call and shouts in the log if it fails.
 */
PasswordCrypt::PasswordCrypt()
{
  if (sodium_init() <
      0)  // The library returns a negative number when setup failed.
  {
    Message::CriticalMessage(
        "Failed to initialize libsodium.");  // Record a serious error in
                                             // the log; do not throw.
  }
}

/**
 * Hashes @p password into a self-describing Argon2id string.
 * In everyday words: ask libsodium to pick a random salt and do the
 *          slow mixing, writing a ready-to-store string into a fixed-size
 * buffer. password: Plaintext password. Returns Encoded hash on success;
 * `std::nullopt` if libsodium could not allocate memory. See also:
 * PasswordCrypt::hashPassword
 */
std::optional<std::string> PasswordCrypt::hashPassword(const std::string&
        password)  // NOLINT  Takes the password, returns a hash or nothing.
{
  char hashed_password[crypto_pwhash_STRBYTES];  // NOLINT  A fixed-size char
                                                 // buffer big enough for any
                                                 // encoded hash.

  // The result is a self-describing string: algorithm, cost, random salt and
  // hash.
  if (crypto_pwhash_str_alg(static_cast<char*>(hashed_password),
          password.c_str(), password.length(), ARGON2ID_OPSLIMIT,
          ARGON2ID_MEMLIMIT,
          crypto_pwhash_ALG_ARGON2ID13) !=
      0)  // NOLINT  Zero means success; anything else means failure.
  {
    // out of memory: the required RAM could not be reserved.
    Message::CriticalMessage(
        "Failed to hash password.");  // Leave a serious error in the log.
    return std::nullopt;  // No hash to give back; caller must handle the
                          // empty result.
  }

  return std::string(static_cast<const char*>(
      hashed_password));  // Copy the buffer into a std::string for the
                          // caller.
}

/**
 * Verifies @p password against @p hash, selecting the algorithm from the hash
 * prefix. In everyday words: look at the label at the front of the stored hash
 *          to decide which recipe was used, then let libsodium redo the slow
 *          mixing and tell us if the result matches.
 * password: Plaintext password.
 * hash: Stored self-describing hash (Argon2id or legacy scrypt).
 * Returns `true` on a match, `false` otherwise (including unknown prefixes).
 * See also: PasswordCrypt::verifyPassword
 */
bool PasswordCrypt::verifyPassword(const std::string& password,
    const std::string&
        hash)  // NOLINT  Returns whether the password is correct.
{
  // The stored string says which algorithm made it, so hashes written before
  // the switch to Argon2id keep working.
  if (hash.starts_with(ARGON2_PREFIX))  // Label says this is an Argon2 hash.
  {
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(),
               password.length()) == 0;  // Zero from libsodium means a match.
  }
  if (hash.starts_with(
          SCRYPT_PREFIX))  // Label says this is an older scrypt hash.
  {
    return crypto_pwhash_scryptsalsa208sha256_str_verify(hash.c_str(),
               password.c_str(),
               password.length()) == 0;  // Same idea for scrypt.
  }
  return false;  // Unknown/foreign format: fail closed (deny) rather than
                 // guessing.
}
