#pragma once

#include <crow.h>

#include <cstdint>
#include <jsoncons/json.hpp>
#include <memory>
#include <regex>
#include <utility>

#include "entities/base/entity.hpp"
#include "fmt/format.h"
#include "utils/message/message.hpp"
#include "utils/resthelper/resthelper.hpp"

#define USERNAME "username"

using json = jsoncons::json;

class Client : public Entity
{
   public:
    struct ClientData
    {
       public:
        ClientData(const json &data, crow::response &res, bool &success)
        {
            for (const auto &item : data.object_range())
            {
                try
                {
                    std::optional<std::string> value = item.value().as<std::string>();
                    if (value.has_value() && !value->empty())
                    {
                        auto pattern_item =
                            std::find_if(validators.begin(), validators.end(), [&](const auto &validator) { return validator.first == item.key(); });

                        if (pattern_item != validators.end())
                        {
                            std::regex pattern(pattern_item->second);
                            if (!std::regex_match(value.value(), pattern))
                            {
                                throw std::runtime_error(fmt::format("Value({}) is invalid.", value.value(), item.key()));
                            }
                        }

                        if (item.key() == "password")
                        {
                            value = passwordCrypt->hashPassword(value.value());
                        }

                        db_data.push_back({item.key(), value.value()});
                    }
                }
                catch (const std::exception &e)
                {
                    RestHelper::failureResponse(res, e.what());
                    return;
                }
            }
            success = true;
        }
        const std::vector<std::pair<std::string, std::string>> &get_data() const { return db_data; }

       protected:
        std::shared_ptr<PasswordCrypt> passwordCrypt = Store::getObject<PasswordCrypt>();

       private:
        std::vector<std::pair<std::string, std::string>> db_data;

        const std::map<std::string, std::string> validators = {
            {"username", "^[a-z][a-z0-9_]*$"},
            {"password", "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[!@#$%^&*])[A-Za-z\\d!@#$%^&*]{8,}$"},
            {"phone", R"(^\+?(\d{1,3})?[-.\s]?(\(?\d{3}\)?)?[-.\s]?\d{3}[-.\s]?\d{4}$)"},
            {"email", R"((\w+)(\.\w+)*@(\w+)(\.\w+)+)"},
            {"dob", R"(^(0[1-9]|[12]\d|3[01])-(0[1-9]|1[0-2])-\d{4}$)"},
            {"gender", "^(male|female)$"},
        };
    };

    struct LogoutData
    {
        std::optional<std::string> token;
        LogoutData(const std::optional<std::string> &_token) { token = _token; }
    };

    struct SuspendData
    {
        uint64_t client_id;
        SuspendData(const uint64_t _client_id) { client_id = _client_id; }
    };
    struct Credentials
    {
        std::string username;  ///< The user's username.
        std::string password;  ///< The user's password.
    };

    Client(const std::string &tablename) : Entity(tablename) {};

    template <typename T>
    Client(const T &data, const std::string &tablename) : Entity(data, tablename)
    {
    }
    /// Generates an SQL query for creating a new client record in the database.
    ///
    /// @return An optional string containing the SQL query, or `std::nullopt` if
    /// an exception occurred.
    std::optional<std::string> getSqlCreateStatement() final
    {
        auto clientdata = std::any_cast<ClientData>(getData());

        try
        {
            std::vector<std::string> keys_arr;
            std::vector<std::string> values_arr;

            for (auto &it : clientdata.get_data())
            {
                keys_arr.push_back(it.first);
                values_arr.push_back(it.second);
            }

            std::string columns = fmt::format("{}", fmt::join(keys_arr, ","));
            std::string values  = fmt::format("'{}'", fmt::join(values_arr, "','"));

            return fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING id;", tablename, columns, values);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql create statement for table {}.", tablename));
            Message::FailureMessage(e.what());
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> getSqlUpdateStatement() final
    {
        std::optional<std::string> query;

        try
        {
            auto                    clientdata = std::any_cast<ClientData>(getData()).get_data();
            std::optional<uint64_t> id;
            auto                    it = std::find_if(clientdata.begin(), clientdata.end(), [&](const auto &item) { return item.first == "id"; });

            if (it != clientdata.end())
            {
                id = std::stoull(it->second);
            }
            else
            {
                throw std::runtime_error("id not found");
            }

            std::string update_column_values;

            for (auto it = clientdata.begin(); it != clientdata.end(); ++it)
            {
                update_column_values.append(fmt::format(" {} = '{}' ", it->first, it->second));
                if (std::next(it) != clientdata.end())
                {
                    update_column_values.append(",");
                }
            }

            query = fmt::format("UPDATE {} set {} WHERE id={} returning id;", tablename, update_column_values, id.value());
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql update statement for table {}.", tablename));
            Message::FailureMessage(e.what());
            return std::nullopt;
        }
        return query;
    }

    inline std::optional<std::string> getSqlToggleSuspendStatement(bool state)
    {
        std::optional<std::string> query;
        try
        {
            SuspendData suspenddata = std::any_cast<SuspendData>(getData());
            query =
                fmt::format("UPDATE {} SET active={} where id={} returning id,active;", tablename, state ? "true" : "false", suspenddata.client_id);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql suspend statement for table {}.", tablename));
            Message::FailureMessage(e.what());
            return std::nullopt;
        }

        return query;
    }

    std::optional<std::string> getSqlSuspendStatement() { return getSqlToggleSuspendStatement(false); }

    std::optional<std::string> getSqlActivateStatement() { return getSqlToggleSuspendStatement(true); }

    bool exists()
    {
        auto client_data = std::any_cast<ClientData>(getData()).get_data();
        auto it          = std::find_if(client_data.begin(), client_data.end(), [&](const auto &item) { return item.first == USERNAME; });

        if (it != client_data.end())
        {
            std::string username = it->second;

            auto result = databaseController->checkItemExists(tablename, USERNAME, username);
            return result.value_or(false);
        }
        return false;
    }

    std::optional<uint64_t> authenticate() const
    {
        Credentials             credentials;
        std::optional<uint64_t> client_id;
        try
        {
            credentials = std::any_cast<Credentials>(getData());
            client_id   = databaseController->findIfUserID(credentials.username, tablename);

            if (!client_id)
                return std::nullopt;

            auto hash = databaseController->getPasswordHashForUserID(client_id.value(), tablename);

            if (!hash)
            {
                return std::nullopt;
            }

            if (passwordCrypt->verifyPassword(credentials.password, hash.value()))
            {
                return client_id;
            }
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Error authenticating client: USERNAME: {} ID: {}", credentials.username,
                                              client_id.has_value() ? std::to_string(client_id.value()) : "N/A"));
            Message::FailureMessage(e.what());
        }
        return std::nullopt;
    }

    virtual ~Client() override = default;

   private:
};
