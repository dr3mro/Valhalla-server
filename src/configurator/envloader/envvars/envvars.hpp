#pragma once

#include <optional>
#include <string>

class EnvVars
{
   public:
    // Method to get an environment variable by name
    std::optional<std::string_view> get(std::string_view key) const;

    // Method to set an environment variable
    void set(std::string_view key, std::string_view value);
};
