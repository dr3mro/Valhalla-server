#include "envloader.hpp"

#include <fmt/core.h>

#include <exception>
#include <sstream>
#include <thread>

std::string EnvLoader::getEnvironmentVariable(const std::string& key, const std::string& defaultValue)
{
    auto value = get(key);
    return value.has_value() ? value.value() : defaultValue;
}

bool EnvLoader::getEnvironmentVariable(const std::string& key, bool defaultValue)
{
    auto value = get(key);
    return value.has_value() ? value.value() == "true" : defaultValue;
}

std::chrono::seconds EnvLoader::getEnvironmentVariable(const std::string& key, const std::chrono::seconds& defaultValue)
{
    auto value = get(key);
    if (!value.has_value())
    {
        return defaultValue;
    }
    try
    {
        auto count = std::stoll(value.value());
        if (count < 0)
        {
            Message::ErrorMessage("Negative duration for env var " + key + "; using default.");
            return defaultValue;
        }
        return std::chrono::seconds(static_cast<uint64_t>(count));
    }
    catch (const std::exception&)
    {
        Message::ErrorMessage("Invalid duration value for env var " + key + "; using default.");
        return defaultValue;
    }
}

std::unordered_set<std::string> EnvLoader::getEnvironmentVariable(const std::string& key)
{
    auto value = get(key);
    return value.has_value() ? parseSet(value.value()) : parseSet("");
}

std::unordered_set<std::string> EnvLoader::parseSet(const std::string& str)
{
    std::unordered_set<std::string> result;
    std::stringstream               ss(str);
    std::string                     item;
    while (std::getline(ss, item, ','))
    {
        // Trim any leading or trailing whitespace
        item.erase(item.find_last_not_of(" \n\r\t") + 1);
        item.erase(0, item.find_first_not_of(" \n\r\t"));
        if (!item.empty())
        {
            result.insert(item);
        }
    }
    return result;
}

void EnvLoader::optimize_performance(uint16_t& parameter, const uint8_t factor)
{
    uint16_t ncpus = (std::thread::hardware_concurrency() > 0) ? std::thread::hardware_concurrency() : 1;
    if (parameter < 1)
    {
        parameter = ncpus * factor;
    }
}
