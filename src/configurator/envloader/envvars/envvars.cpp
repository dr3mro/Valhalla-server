#include "envvars.hpp"

#include <fmt/format.h>

#include <cstdlib>

#include "utils/message/message.hpp"

std::optional<std::string> EnvVars::get(const std::string& key)
{
    // Retrieve the value associated with the environment variable key
    const char* _key  = key.c_str();
    const char* value = std::getenv(_key);

    // If the value exists, return it as a string_view
    if (value != nullptr)
    {
        return std::string(value);
    }

    // Otherwise, return an empty optional
    return std::nullopt;
}

void EnvVars::set(const std::string& key, const std::string& value)
{
    std::string envVar = fmt::format("{}={}", key, value);
    if (putenv(const_cast<char*>(envVar.c_str())) != 0)
    {
        Message::ErrorMessage(fmt::format("Failed to set environment variable {}.", envVar));
    }
}
