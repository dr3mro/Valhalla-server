#include "passwordcrypt.hpp"

#include <sodium/crypto_pwhash.h>
#include <sodium/crypto_pwhash_scryptsalsa208sha256.h>

#include <cstddef>
#include <optional>
#include <string>

#include "sodium/core.h"
#include "utils/message/message.hpp"

namespace
{
    // Argon2id at OWASP's minimum recommended cost (19 MiB of memory, 2 passes, 1 lane; libsodium always uses one lane).
    // The work runs on the PasswordWorker threads, never on a request thread.
    constexpr unsigned long long ARGON2ID_OPSLIMIT = 2;
    constexpr std::size_t        ARGON2ID_MEMLIMIT = 19UL * 1024UL * 1024UL;

    constexpr const char* ARGON2_PREFIX = "$argon2";  // $argon2id$... (and $argon2i$... if one is ever imported)
    constexpr const char* SCRYPT_PREFIX = "$7$";      // libsodium's scrypt format, written by older versions of this server
}  // namespace

PasswordCrypt::PasswordCrypt()
{
    if (sodium_init() < 0)
    {
        Message::CriticalMessage("Failed to initialize libsodium.");
    }
}

std::optional<std::string> PasswordCrypt::hashPassword(const std::string& password)  // NOLINT
{
    char hashed_password[crypto_pwhash_STRBYTES];  // NOLINT

    // The result is a self-describing string: algorithm, cost, random salt and hash.
    if (crypto_pwhash_str_alg(static_cast<char*>(hashed_password), password.c_str(), password.length(), ARGON2ID_OPSLIMIT, ARGON2ID_MEMLIMIT,
            crypto_pwhash_ALG_ARGON2ID13) != 0)  // NOLINT
    {
        // out of memory
        Message::CriticalMessage("Failed to hash password.");
        return std::nullopt;
    }

    return std::string(static_cast<const char*>(hashed_password));
}

bool PasswordCrypt::verifyPassword(const std::string& password, const std::string& hash)  // NOLINT
{
    // The stored string says which algorithm made it, so hashes written before the switch to Argon2id keep working.
    if (hash.starts_with(ARGON2_PREFIX))
    {
        return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
    }
    if (hash.starts_with(SCRYPT_PREFIX))
    {
        return crypto_pwhash_scryptsalsa208sha256_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
    }
    return false;
}
