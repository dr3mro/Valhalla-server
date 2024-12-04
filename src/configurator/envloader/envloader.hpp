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
    T getEnvironmentVariable(std::string_view key, T defaultValue)
    {
        auto value = get(key);
        return value ? std::stoi(value.value().data()) : defaultValue;
    }

    std::string_view     getEnvironmentVariable(std::string_view key, std::string_view defaultValue);
    bool                 getEnvironmentVariable(std::string_view key, bool defaultValue);
    std::chrono::seconds getEnvironmentVariable(std::string_view key, const std::chrono::seconds &defaultValue);
    std::unordered_set<std::string> getEnvironmentVariable(std::string_view key);

    virtual void printValues() const = 0;

   protected:
    void                            optimize_performance(uint16_t &parameter, uint8_t factor);
    std::unordered_set<std::string> parseSet(const std::string_view &str);
};
