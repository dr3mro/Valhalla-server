#include "envvars.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <iostream>

#include "utils/message/message.hpp"

std::optional<std::string_view> EnvVars::get(std::string_view key) const
{
    const char *value = std::getenv(key.data());
    if (value)
    {
        return value;
    }
    return std::nullopt;
}

void EnvVars::set(std::string_view key, std::string_view value)
{
    std::string envVar = fmt::format("{}={}", key, value);
    if (putenv(const_cast<char *>(envVar.c_str())) != 0)
    {
        Message::ErrorMessage(fmt::format("Failed to set environment variable {}.", envVar));
    }
}
