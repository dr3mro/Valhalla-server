#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <sodium/crypto_pwhash_scryptsalsa208sha256.h>
#include <string>

#include "gatekeeper/passwordcrypt/passwordcrypt.hpp"

using ::PasswordCrypt;

TEST_CASE("PasswordCrypt hashes and verifies a password", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    const auto hash = crypt.hashPassword("S3cure!Passw0rd");
    REQUIRE(hash.has_value());
    REQUIRE_FALSE(hash->empty());
    REQUIRE(crypt.verifyPassword("S3cure!Passw0rd", hash.value()));
}

TEST_CASE("PasswordCrypt rejects a wrong password", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    const auto hash = crypt.hashPassword("correct horse battery staple");
    REQUIRE(hash.has_value());
    REQUIRE_FALSE(crypt.verifyPassword("incorrect horse battery staple", hash.value()));
}

TEST_CASE("PasswordCrypt salts: hashing the same password twice yields different hashes", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    const auto first  = crypt.hashPassword("same-password");
    const auto second = crypt.hashPassword("same-password");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value() != second.value());

    // But both still verify.
    CHECK(crypt.verifyPassword("same-password", first.value()));
    CHECK(crypt.verifyPassword("same-password", second.value()));
}

TEST_CASE("PasswordCrypt handles boundary inputs", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    const auto& password =
        GENERATE(std::string(""), std::string("a"), std::string(512, 'x'), std::string("pässwörd-密码-🔐"), std::string("with space and\ttab"));

    CAPTURE(password.size());
    const auto hash = crypt.hashPassword(password);
    REQUIRE(hash.has_value());
    REQUIRE(crypt.verifyPassword(password, hash.value()));
    REQUIRE_FALSE(crypt.verifyPassword(password + "x", hash.value()));
}

TEST_CASE("PasswordCrypt does not crash on malformed or truncated hashes", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    const auto& badHash =
        GENERATE(std::string(""), std::string("garbage"), std::string("$scrypt$"), std::string("$7$C6..../....salt$truncated"), std::string("$argon2id$v=19$m=19456,t=2,p=1$truncated"), std::string(64, '\0'));

    CAPTURE(badHash.size());
    REQUIRE_FALSE(crypt.verifyPassword("any password", badHash));
}

TEST_CASE("PasswordCrypt hash output is an Argon2id encoded string", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    const auto hash = crypt.hashPassword("format-check");
    REQUIRE(hash.has_value());
    // libsodium's Argon2id str format: $argon2id$v=19$m=<KiB>,t=<passes>,p=1$<salt>$<hash>
    REQUIRE(hash->starts_with("$argon2id$"));
    CHECK(hash->find("m=19456,t=2,p=1") != std::string::npos);  // OWASP minimum: 19 MiB, 2 passes
}

TEST_CASE("PasswordCrypt still verifies hashes made with the old scrypt scheme", "[gatekeeper][password]")
{
    PasswordCrypt crypt;

    // A hash as the previous version of the server stored it (scrypt, the cost it used).
    char legacy[crypto_pwhash_scryptsalsa208sha256_STRBYTES];  // NOLINT
    REQUIRE(crypto_pwhash_scryptsalsa208sha256_str(static_cast<char*>(legacy), "old-password", 12, crypto_pwhash_scryptsalsa208sha256_OPSLIMIT_MIN,
                crypto_pwhash_scryptsalsa208sha256_MEMLIMIT_MIN) == 0);
    const std::string legacy_hash(static_cast<const char*>(legacy));
    REQUIRE(legacy_hash.starts_with("$7$"));

    CHECK(crypt.verifyPassword("old-password", legacy_hash));
    CHECK_FALSE(crypt.verifyPassword("other-password", legacy_hash));
}

TEST_CASE("PasswordCrypt rejects a hash with an unknown algorithm prefix", "[gatekeeper][password]")
{
    PasswordCrypt crypt;
    CHECK_FALSE(crypt.verifyPassword("any", "$2b$12$abcdefghijklmnopqrstuuABCDEFGHIJKLMNOPQRSTUVWXYZ01234"));
    CHECK_FALSE(crypt.verifyPassword("any", "plaintext-password"));
}
