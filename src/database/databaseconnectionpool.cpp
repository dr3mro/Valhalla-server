#include "databaseconnectionpool.hpp"

#include <future>
#include <pqxx/pqxx>

#include "utils/message/message.hpp"

std::shared_ptr<Database> DatabaseConnectionPool::createDatabaseConnection()
{
    auto command =
        fmt::format("host= {}  dbname= {}  user= {} password= {} connect_timeout=2", config_.host, config_.name, config_.user, config_.pass);

    auto conn = std::make_shared<pqxx::connection>(command.c_str());

    if (conn->is_open())
    {
        return std::make_shared<Database>(conn);
    }
    else
    {
        throw std::runtime_error("Failed to open database connection");
    }
}

DatabaseConnectionPool::DatabaseConnectionPool()
{
    try
    {
        for (uint16_t i = 0; i < config_.max_conn; ++i)
        {
            const int MAX_RETRIES           = 3;
            int       retryCount            = 0;
            bool      connectionEstablished = false;

            while (retryCount < MAX_RETRIES && !connectionEstablished)
            {
                auto future = std::async(std::launch::async, &DatabaseConnectionPool::createDatabaseConnection, this);
                auto status = future.wait_for(std::chrono::seconds(30));

                if (status == std::future_status::ready)
                {
                    databaseConnections.push(future.get());
                    Message::InitMessage(fmt::format("Connection {}/{} created successfully.", i + 1, config_.max_conn));
                    connectionEstablished = true;
                }
                else
                {
                    // std::this_thread::sleep_for(std::chrono::seconds(1));
                    retryCount++;
                    if (retryCount < MAX_RETRIES)
                    {
                        Message::WarningMessage(fmt::format("Connection attempt {} timed out, retrying... ({}/{})", i + 1, retryCount, MAX_RETRIES));
                        // Exponential backoff
                        std::this_thread::sleep_for(std::chrono::seconds(1 << retryCount));
                    }
                }
            }

            if (!connectionEstablished)
            {
                Message::ErrorMessage(fmt::format("Failed to establish connection {} after {} attempts.", i + 1, MAX_RETRIES));
                throw std::runtime_error("Database connection pool initialization failed");
            }
        }
    }
    catch (const std::exception &e)
    {
        Message::ErrorMessage("Exception caught during database connection pool initialization.");
        Message::CriticalMessage(e.what());
        Message::InfoMessage("Make sure the database server is running and reachable and the connection parameters are correct.");
        Message::CriticalMessage("Failed to initialize database connection pool.");
        exit(EXIT_FAILURE);
    }
}

std::shared_ptr<Database> DatabaseConnectionPool::get_connection()
{
    std::unique_lock<std::mutex> lock(mutex);
    while (databaseConnections.empty())
    {
        cv.wait(lock);
    }
    auto db = databaseConnections.front();
    databaseConnections.pop();
    return db;
}

void DatabaseConnectionPool::return_connection(std::shared_ptr<Database> db)
{
    std::lock_guard<std::mutex> lock(mutex);
    databaseConnections.push(std::move(db));
    cv.notify_one();
}
