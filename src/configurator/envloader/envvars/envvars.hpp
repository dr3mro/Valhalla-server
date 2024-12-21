#pragma once

#include <optional>
#include <string>

class EnvVars
{
   protected:
    // Method to get an environment variable by name
    static std::optional<std::string> get(const std::string& key);

    // Method to set an environment variable
    static void set(const std::string& key, const std::string& value);
};
