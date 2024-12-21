#pragma once

#include <chrono>
#include <string>
#include <unordered_set>

#include "envvars/envvars.hpp"

class EnvLoader : public EnvVars
{
   public:
    explicit EnvLoader() = default;
    virtual ~EnvLoader() = default;

    template <typename T>
    T getEnvironmentVariable(const std::string& key, T defaultValue)
    {
        auto value = get(key);
        return value ? std::stoi(value.value()) : defaultValue;
    }

    std::string                     getEnvironmentVariable(const std::string& key, const std::string& defaultValue);
    bool                            getEnvironmentVariable(const std::string& key, bool defaultValue);
    std::chrono::seconds            getEnvironmentVariable(const std::string& key, const std::chrono::seconds& defaultValue);
    std::unordered_set<std::string> getEnvironmentVariable(const std::string& key);

    virtual void printValues() const = 0;

   protected:
    void                            optimize_performance(uint16_t& parameter, uint8_t factor);
    std::unordered_set<std::string> parseSet(const std::string& str);
};
