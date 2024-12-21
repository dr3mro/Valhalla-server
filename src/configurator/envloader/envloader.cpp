#include "envloader.hpp"

#include <fmt/core.h>

#include <sstream>
#include <thread>

std::string EnvLoader::getEnvironmentVariable(const std::string &key, const std::string &defaultValue)
{
    auto value = get(key);
    return value.has_value() ? value.value() : defaultValue;
}

bool EnvLoader::getEnvironmentVariable(const std::string &key, bool defaultValue)
{
    auto value = get(key);
    return value.has_value() ? value.value() == "true" : defaultValue;
}

std::chrono::seconds EnvLoader::getEnvironmentVariable(const std::string &key, const std::chrono::seconds &defaultValue)
{
    auto value = get(key);
    return value.has_value() ? std::chrono::seconds(std::stoull(value.value())) : defaultValue;
}

std::unordered_set<std::string> EnvLoader::getEnvironmentVariable(const std::string &key)
{
    auto value = get(key);
    return value.has_value() ? parseSet(value.value()) : parseSet("");
}

std::unordered_set<std::string> EnvLoader::parseSet(const std::string &str)
{
    std::unordered_set<std::string> result;
    std::stringstream               ss(str);
    std::string                     item;
    while (std::getline(ss, item, ','))
    {
        // Trim any leading or trailing whitespace
        item.erase(item.find_last_not_of(" \n\r\t") + 1);
        item.erase(0, item.find_first_not_of(" \n\r\t"));
        result.insert(item.c_str());
    }
    return result;
}

void EnvLoader::optimize_performance(uint16_t &parameter, const uint8_t factor)
{
    uint16_t ncpus = (std::thread::hardware_concurrency() > 0) ? std::thread::hardware_concurrency() : 1;
    if (parameter < 1)
    {
        parameter = ncpus * factor;
    }
}
