#include "database.hpp"

#include <fmt/core.h>

#include <cstdlib>
#include <utility>

#include "utils/message/message.hpp"

Database::Database(std::shared_ptr<pqxx::connection> conn) : connection(std::move(conn))
{
    try
    {
        if (connection->is_open())
        {
            // Execute a simple query to test the connection
            pqxx::nontransaction ntxn(*connection);
            ntxn.exec("SELECT 1");
        }
        else
        {
            Message::CriticalMessage("Failed to open database connection.");
            exit(EXIT_FAILURE);
        }
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception caught during database connection.");
        Message::CriticalMessage(e.what());
        exit(EXIT_FAILURE);
    }
}

bool Database::isConnected() { return connection && connection->is_open(); }

bool Database::checkExists(const std::string &table, const std::string &column, const std::string &value)
{
    try
    {
        pqxx::nontransaction txn(*connection);
        pqxx::result         result = txn.exec(fmt::format("SELECT EXISTS ( SELECT 1 FROM {} WHERE {} = '{}');", table, column, value));
        return result[0][0].as<bool>();
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Error executing query.");
        Message::CriticalMessage(e.what());
        return false;
    }
}
