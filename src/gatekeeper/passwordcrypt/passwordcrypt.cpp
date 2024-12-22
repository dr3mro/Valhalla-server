#include "passwordcrypt.hpp"

#include <sodium/crypto_pwhash_scryptsalsa208sha256.h>

#include <cstring>
#include <optional>
#include <string>

#include "sodium/core.h"
#include "sodium/randombytes.h"
#include "utils/message/message.hpp"
PasswordCrypt::PasswordCrypt()
{
    if (sodium_init() < 0)
    {
        Message::CriticalMessage("Failed to initialize libsodium.");
    }
}

std::optional<std::string> PasswordCrypt::hashPassword(const std::string &password)
{
    std::optional<std::string> hash;

    // Salt for the hashing (must be random)
    unsigned char salt[crypto_pwhash_scryptsalsa208sha256_SALTBYTES];
    randombytes_buf(salt, sizeof salt);

    // Buffer to store the hashed password
    unsigned char hashed_password[crypto_pwhash_scryptsalsa208sha256_STRBYTES];

    // Hash the password using the scrypt algorithm
    if (crypto_pwhash_scryptsalsa208sha256_str((char *)hashed_password, password.c_str(), password.length(), crypto_pwhash_scryptsalsa208sha256_OPSLIMIT_MIN,
            crypto_pwhash_scryptsalsa208sha256_MEMLIMIT_MIN) != 0)
    {
        // out of memory
        Message::CriticalMessage("Failed to hash password.");
        return std::nullopt;
    }

    hash = std::string(reinterpret_cast<char *>(hashed_password));
    return hash;
}
bool PasswordCrypt::verifyPassword(const std::string &password, const std::string &hash)
{
    // Verify the password using the scrypt algorithm
    return crypto_pwhash_scryptsalsa208sha256_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
}
