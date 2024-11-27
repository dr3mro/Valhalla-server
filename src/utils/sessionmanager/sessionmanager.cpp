#include "sessionmanager.hpp"

#include <fmt/chrono.h>

#include <chrono>
#include <ctime>
#include <string>

#include "controllers/clientcontroller/clientcontroller.hpp"
#include "fmt/core.h"
#include "utils/message/message.hpp"

std::optional<std::string> SessionManager::setNowLoginTime(uint64_t id, const std::string &group)
{
    try
    {
        std::string login_time = current_time_to_utc_string();

        std::string query = fmt::format(
            "INSERT INTO {}_sessions (id, last_login,last_logout) VALUES ({}, '{}', '{}') "
            "ON CONFLICT (id) DO UPDATE SET last_login = EXCLUDED.last_login RETURNING last_logout;",
            group, id, login_time, login_time);

        auto result = databaseController->executeQuery(query);
        if (!result.has_value())
        {
            Message::ErrorMessage("Error updating login time.");
            Message::CriticalMessage(result.value().to_string());
        }
        else if (!result->empty())
        {
            return result.value().at("last_logout").as_string();
        }
        return std::nullopt;
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error updating login time.");
        Message::CriticalMessage(e.what());
    }
    return std::nullopt;
}

void SessionManager::setNowLogoutTime(uint64_t id, const std::string &group)
{
    try
    {
        std::string logout_time = current_time_to_utc_string();

        std::string query = fmt::format(
            "INSERT INTO {}_sessions (id, last_logout) VALUES ({}, '{}') "
            "ON CONFLICT (id) DO UPDATE SET last_logout = EXCLUDED.last_logout;",
            group, id, logout_time);

        auto result = databaseController->executeQuery(query);
        if (!result.has_value())
        {
            Message::ErrorMessage("Error updating login time.");
            Message::CriticalMessage(result.value().to_string());
        }
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error updating logout time.");
        Message::CriticalMessage(e.what());
    }
}

std::optional<std::string> SessionManager::getLastLoginTime(uint64_t id, const std::string &group)
{
    std::string query = fmt::format("SELECT last_login FROM {}_sessions WHERE id = {};", group, id);
    return databaseController->doReadQuery(query);
}

bool SessionManager::getLastLogoutTimeIfActive(LoggedClientInfo &loggedClientInfo)
{
    try
    {
        std::string query = fmt::format(
            "WITH client_data AS (SELECT ses.id, ses.last_logout, client.active FROM {}_sessions ses\
            LEFT JOIN {} client ON ses.id = client.id) \
            SELECT id,\
            last_logout, active FROM client_data WHERE id = {};",
            loggedClientInfo.group.value(), loggedClientInfo.group.value(), loggedClientInfo.clientId.value());

        auto results = databaseController->executeReadQuery(query);
        if (results.has_value())
        {
            if (!results.value().empty())
            {
                loggedClientInfo.is_active = results.value().at("active").as_bool();
                loggedClientInfo.llodt     = results.value().at("last_logout").as_string();
                return true;
            }
        }
        return false;
    }
    catch (std::exception &e)
    {
        CRITICALMESSAGE
    }
    return false;
}

std::string SessionManager::current_time_to_utc_string()
{
    // Get current time as a time_point
    auto now = std::chrono::system_clock::now();

    // Convert time_point to time_t
    auto now_t = std::chrono::system_clock::to_time_t(now);

    // Convert time_t to tm structure in UTC
    std::tm tm = *std::gmtime(&now_t);

    // Format the time using fmt::format
    std::string formatted_time = fmt::format("{:%Y-%m-%d %H:%M:%S} +0000", tm);

    return formatted_time;
}
