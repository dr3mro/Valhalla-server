#include "envvars.hpp"

#include <fmt/format.h>

#include <cstdlib>

#include "utils/message/message.hpp"

std::optional<std::string_view> EnvVars::get(std::string_view key)
{
    // Retrieve the value associated with the environment variable key
    const char *_key  = key.data();
    const char *value = std::getenv(_key);

    // If the value exists, return it as a string_view
    if (value != nullptr)
    {
        return std::string_view(value);
    }

    // Otherwise, return an empty optional
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
