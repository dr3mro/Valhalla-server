#pragma once

#include <jsoncons/json.hpp>
#include <memory>
#include <utility>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/entity.hpp"
#include "fmt/format.h"
#include "store/store.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"

#define USERNAME "username"

using json = jsoncons::json;

/// Represents a client entity in the application. The `Client` class inherits
/// from the `Entity` class and provides functionality for managing
/// client-related data, including authentication, validation, and database
/// operations.
///
/// The `Client` class has the following key features:
/// - Retrieves the `DatabaseController` and `PasswordCrypt` instances from the
/// `Store` to handle database operations and password encryption/decryption.
/// - Provides a `getSqlCreateStatement()` method to generate an SQL query for
/// creating a new client record in the database.
/// - Implements an `exists()` method to check if a client with a given username
/// already exists in the database.
/// - Provides an `authenticate()` method to authenticate a client using their
/// username and password.
/// - Implements a `validate()` method to validate the client's data, including
/// username, password, and email. Represents a client entity in the
/// application. The `Client` class inherits from the `Entity` class and
/// provides functionality for managing client-related data, including
/// authentication, validation, and database operations.
///
/// The `Client` class has the following key features:
/// - Retrieves the `DatabaseController` and `PasswordCrypt` instances from the
/// `Store` to handle database operations and password encryption/decryption.
/// - Provides a `getSqlCreateStatement()` method to generate an SQL query for
/// creating a new client record in the database.
/// - Implements an `exists()` method to check if a client with a given username
/// already exists in the database.
/// - Provides an `authenticate()` method to authenticate a client using their
/// username and password.
/// - Implements a `validate()` method to validate the client's data, including
/// username, password, and email.
class Client : public Entity
{
   public:
    struct Credentials
    {
        std::string username;  ///< The user's username.
        std::string password;  ///< The user's password.
    };

    template <typename T>
    /// Constructs a new `Client` object with the given data and table name.
    ///
    /// @param data The data to initialize the `Client` object with.
    /// @param tablename The name of the database table associated with the
    /// `Client` object.
    Client(const T &data, const std::string &tablename) : Entity(data, tablename)

    {
        try
        {
            databaseController = Store::getObject<DatabaseController>();
            passwordCrypt      = Store::getObject<PasswordCrypt>();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception caught in Client constructor: " << e.what() << '\n';
            exit(EXIT_FAILURE);
        }
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

            for (auto &it : clientdata.get_db_data())
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
            std::cerr << "faild to create query for create " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> getSqlUpdateStatement() final
    {
        std::optional<std::string> query;

        try
        {
            auto                    clientdata = std::any_cast<ClientData>(getData()).get_db_data();
            std::optional<uint64_t> id;
            auto                    it = std::find_if(clientdata.begin(), clientdata.end(), [&](const auto &item) { return item.first == "id"; });

            if (it != clientdata.end())
            {
                id = std::stoull(it->second);
            }
            else
            {
                throw std::runtime_error("id not found");
                return std::nullopt;
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
            std::cerr << "failed to create query for update " << tablename << e.what() << '\n';
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
            std::cerr << fmt::format("faild to create query for suspend {}: {} \n", tablename, e.what());
            return std::nullopt;
        }

        return query;
    }

    std::optional<std::string> getSqlSuspendStatement() { return getSqlToggleSuspendStatement(false); }

    std::optional<std::string> getSqlActivateStatement() { return getSqlToggleSuspendStatement(true); }

    bool exists()
    {
        auto client_data = std::any_cast<ClientData>(getData()).get_db_data();
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
        try
        {
            auto credentials = std::any_cast<Credentials>(getData());
            auto client_id   = databaseController->findIfUserID(credentials.username, tablename);

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
            std::cerr << "Error authenticating user : " << e.what() << '\n';
        }
        return std::nullopt;
    }

    ~Client() override = default;

   private:
    std::shared_ptr<DatabaseController> databaseController;
    std::shared_ptr<PasswordCrypt>      passwordCrypt;
};
