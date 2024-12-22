#pragma once
#include <optional>
#include <string>

class PasswordCrypt
{
   public:
    PasswordCrypt();
    PasswordCrypt(const PasswordCrypt &)            = default;
    PasswordCrypt(PasswordCrypt &&)                 = default;
    PasswordCrypt &operator=(const PasswordCrypt &) = default;
    PasswordCrypt &operator=(PasswordCrypt &&)      = default;
    virtual ~PasswordCrypt()                        = default;

    std::optional<std::string> hashPassword(const std::string &password);
    [[nodiscard]] bool         verifyPassword(const std::string &password, const std::string &hash);
};
