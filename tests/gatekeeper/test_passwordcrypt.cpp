/**
 * @file test_passwordcrypt.cpp
 * @brief Catch2 unit tests and fixtures for passwordcrypt. Checks hashing,
 * verification, salting, boundary inputs and backward compatibility with legacy
 * scrypt hashes.
 * @details PasswordCrypt must be robust at every boundary: hashing with a fresh
 * salt so identical passwords produce different hashes, verifying both current
 * Argon2id hashes and old scrypt hashes, and rejecting malformed or truncated
 * stored hashes without ever throwing. The tests also check the exact Argon2id
 * string format and its cost parameters, and that wrong passwords, unknown
 * algorithms and corrupt data simply fail to match.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects passwordcrypt.hpp, the component that stores and verifies user
 * passwords. A salt bug would make identical passwords share a hash, exposing
 * all such accounts at once; failing to verify legacy scrypt hashes would lock
 * existing users out after the migration; an exception on a corrupt stored hash
 * could crash the login path. These tests keep authentication both secure and
 * available.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 data generators (GENERATE) to reuse one test across many inputs:
 * https://github.com/catchorg/Catch2/blob/devel/docs/generators.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - Password hashing with Argon2id and scrypt (salts, cost, verification):
 * https://doc.libsodium.org/password_hashing/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// libsodium's old scrypt API, used to create a legacy hash for compatibility
// testing.
#include <sodium/crypto_pwhash_scryptsalsa208sha256.h>

// Provides TEST_CASE and the CHECK / REQUIRE family of macros.
#include <catch2/catch_test_macros.hpp>
// GENERATE, which runs the same assertions over a list of input values.
#include <catch2/generators/catch_generators.hpp>
#include <string>  // std::string for passwords and encoded hashes.

// The production header under test: PasswordCrypt.
#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"

using ::PasswordCrypt;  // Alias for the global PasswordCrypt class under test.

// PasswordCrypt must be robust at every boundary: hashing with fresh salts,
// verifying both current Argon2id hashes and legacy scrypt hashes, and
// rejecting malformed input without ever throwing.

TEST_CASE("PasswordCrypt hashes and verifies a password",
          "[gatekeeper][password]")  // Registers the basic round-trip test.
{
  PasswordCrypt crypt;  // The hasher/verifier under test.

  const auto hash = crypt.hashPassword(
      "S3cure!Passw0rd");     // Hash a sample password; the result is optional
                              // because hashing can fail.
  REQUIRE(hash.has_value());  // Prove hashing succeeded before we open the
                              // result.
  REQUIRE_FALSE(hash->empty());  // Prove the hash is a real non-empty string,
                                 // not a blank placeholder.
  REQUIRE(crypt.verifyPassword("S3cure!Passw0rd",
                               hash.value()));  // Prove the original password
                                                // verifies against its own hash
                                                // (the core contract).
}

TEST_CASE("PasswordCrypt rejects a wrong password",
          "[gatekeeper][password]")  // Registers the test that a near-miss
                                     // password is refused.
{
  PasswordCrypt crypt;  // The hasher/verifier under test.

  const auto hash = crypt.hashPassword(
      "correct horse battery staple");  // Hash the correct password.
  REQUIRE(hash.has_value());  // Prove we really got a hash to test against.
  REQUIRE_FALSE(
      crypt.verifyPassword("incorrect horse battery staple",
                           hash.value()));  // Prove a different password does
                                            // not verify, even though it is
                                            // similar.
}

TEST_CASE(
    "PasswordCrypt salts: hashing the same password twice yields different "
    "hashes",
    "[gatekeeper][password]")  // Registers the test that each hash gets a fresh
                               // random salt.
{
  PasswordCrypt crypt;  // The hasher/verifier under test.

  const auto first =
      crypt.hashPassword("same-password");  // First hash of a fixed password.
  const auto second = crypt.hashPassword(
      "same-password");         // Second hash of the identical password.
  REQUIRE(first.has_value());   // Prove the first hash succeeded.
  REQUIRE(second.has_value());  // Prove the second hash succeeded.
  REQUIRE(first.value() !=
          second.value());  // Prove the two hashes differ, so identical
                            // passwords are not visibly the same in storage.

  // But both still verify.
  CHECK(
      crypt.verifyPassword("same-password",
                           first.value()));  // Prove salting did not break
                                             // verification of the first hash.
  CHECK(crypt.verifyPassword("same-password",
                             second.value()));  // Prove the second hash is
                                                // equally usable.
}

TEST_CASE("PasswordCrypt handles boundary inputs",
          "[gatekeeper][password]")  // Registers the test for unusual password
                                     // shapes.
{
  PasswordCrypt crypt;  // The hasher/verifier under test.

  // Empty, single-character, long, non-ASCII/multibyte, and embedded-
  // whitespace passwords all exercise libsodium's length and encoding paths.
  const auto& password =
      GENERATE(std::string(""),
               std::string("a"),
               std::string(512, 'x'),
               std::string("pässwörd-密码-🔐"),
               std::string(
                   "with space and\ttab"));  // Catch2 re-runs the whole test
                                             // once per value, covering empty,
                                             // short, long, Unicode and
                                             // whitespace cases.

  CAPTURE(password.size());  // Print the length on failure; printing the
                             // password itself would be unsafe.
  const auto hash = crypt.hashPassword(
      password);  // Hash whichever boundary value this run selected.
  REQUIRE(hash.has_value());  // Prove even the odd input can be hashed.
  REQUIRE(
      crypt.verifyPassword(password, hash.value()));  // Prove the exact same
                                                      // input verifies.
  REQUIRE_FALSE(
      crypt.verifyPassword(password + "x",
                           hash.value()));  // Prove appending one character
                                            // makes verification fail.
}

TEST_CASE("PasswordCrypt does not crash on malformed or truncated hashes",
          "[gatekeeper][password]")  // Registers the robustness test for
                                     // corrupt stored data.
{
  PasswordCrypt crypt;  // The verifier under test.

  const auto& badHash =
      GENERATE(std::string(""),
               std::string("garbage"),
               std::string("$scrypt$"),
               std::string("$7$C6..../....salt$truncated"),
               std::string("$argon2id$v=19$m=19456,t=2,p=1$truncated"),
               std::string(64,
                           '\0'));  // A spread of broken encodings: empty,
                                    // junk, truncated scrypt/Argon2 strings and
                                    // embedded nulls.

  // A stored hash is untrusted input: verifyPassword must treat every
  // malformed/truncated encoding as a failed match, never as an exception.
  CAPTURE(badHash.size());  // Report only the size on failure, since the
                            // contents may be unprintable.
  REQUIRE_FALSE(
      crypt.verifyPassword("any password",
                           badHash));  // Prove each bad hash fails to match
                                       // instead of throwing or crashing.
}

TEST_CASE("PasswordCrypt hash output is an Argon2id encoded string",
          "[gatekeeper][password]")  // Registers the test that the stored
                                     // format and cost are pinned down.
{
  PasswordCrypt crypt;  // The hasher under test.

  const auto hash =
      crypt.hashPassword("format-check");  // Produce a hash to inspect.
  REQUIRE(hash.has_value());               // Prove hashing succeeded.
  // libsodium's Argon2id str format:
  // $argon2id$v=19$m=<KiB>,t=<passes>,p=1$<salt>$<hash>
  REQUIRE(hash->starts_with("$argon2id$"));  // Prove the algorithm prefix is
                                             // Argon2id, the current scheme.
  CHECK(hash->find("m=19456,t=2,p=1") !=
        std::string::npos);  // OWASP minimum: 19 MiB, 2 passes -- locks in the
                             // intended cost so it cannot silently weaken.
}

TEST_CASE("PasswordCrypt still verifies hashes made with the old scrypt scheme",
          "[gatekeeper][password]")  // Registers the backward-compatibility
                                     // test for migrated accounts.
{
  PasswordCrypt crypt;  // The verifier under test.

  // Backward-compatibility contract: hashes written by the previous server
  // version must remain verifiable after the migration to Argon2id.
  // A hash as the previous version of the server stored it (scrypt, the cost
  // it used).
  char legacy[crypto_pwhash_scryptsalsa208sha256_STRBYTES];  // NOLINT  //
                                                             // Buffer sized by
                                                             // libsodium to
                                                             // hold one encoded
                                                             // scrypt hash.
  REQUIRE(crypto_pwhash_scryptsalsa208sha256_str(
              static_cast<char*>(legacy),
              "old-password",
              12,
              crypto_pwhash_scryptsalsa208sha256_OPSLIMIT_MIN,
              crypto_pwhash_scryptsalsa208sha256_MEMLIMIT_MIN) ==
          0);  // Generate a genuine legacy hash using the old API; a zero
               // return means success.
  const std::string legacy_hash(static_cast<const char*>(
      legacy));  // Copy the C buffer into a std::string for the verifier.
  REQUIRE(legacy_hash.starts_with("$7$"));  // Prove it really is an scrypt hash
                                            // (that is the legacy prefix).

  CHECK(crypt.verifyPassword("old-password",
                             legacy_hash));  // Prove the current verifier still
                                             // accepts old hashes, so existing
                                             // users are not locked out.
  CHECK_FALSE(crypt.verifyPassword("other-password",
                                   legacy_hash));  // Prove the old format still
                                                   // rejects a wrong password.
}

TEST_CASE("PasswordCrypt rejects a hash with an unknown algorithm prefix",
          "[gatekeeper][password]")  // Registers the test for unrecognised
                                     // encoding schemes.
{
  PasswordCrypt crypt;  // The verifier under test.
  CHECK_FALSE(crypt.verifyPassword(
      "any",
      // A bcrypt-style prefix is not supported, so it must simply fail.
      "$2b$12$abcdefghijklmnopqrstuuABCDEFGHIJKLMNOPQRSTUVWXYZ01234"));
  CHECK_FALSE(
      crypt.verifyPassword("any",
                           "plaintext-password"));  // Plain text is not a hash
                                                    // at all, so it must also
                                                    // fail cleanly.
}
