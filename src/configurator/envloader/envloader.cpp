#include "envloader.hpp"

#include <fmt/core.h>

#include <sstream>
#include <thread>

std::string_view EnvLoader::getEnvironmentVariable(std::string_view key, std::string_view defaultValue)
{
    auto value = get(key);
    return value ? value.value() : defaultValue;
}

bool EnvLoader::getEnvironmentVariable(std::string_view key, bool defaultValue)
{
    auto value = get(key);
    return value ? value.value() == "true" : defaultValue;
}

std::chrono::seconds EnvLoader::getEnvironmentVariable(std::string_view key, const std::chrono::seconds &defaultValue)
{
    auto value = get(key);
    return value ? std::chrono::seconds(std::stoi(value.value().data())) : defaultValue;
}

std::unordered_set<std::string> EnvLoader::getEnvironmentVariable(std::string_view key)
{
    auto value = get(key);
    return value ? parseSet(value.value()) : parseSet("");
}

std::unordered_set<std::string> EnvLoader::parseSet(const std::string_view &str)
{
    std::unordered_set<std::string> result;
    std::stringstream               ss(str.data());
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
