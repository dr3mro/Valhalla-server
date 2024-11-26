#pragma once

#include <optional>
#include <string_view>

class EnvVars
{
   protected:
    // Method to get an environment variable by name
    static std::optional<std::string_view> get(std::string_view key);

    // Method to set an environment variable
    static void set(std::string_view key, std::string_view value);
};
