#include "databaseconnectionpool.hpp"

#include <fmt/core.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <stdexcept>
#include <thread>
#include <utility>

#include "database/database.hpp"
#include "utils/message/message.hpp"

std::shared_ptr<Database> DatabaseConnectionPool::createDatabaseConnection()
{
    try
    {
        auto command = fmt::format("host={} dbname={} user={} password={} connect_timeout=2", config_.host, config_.name, config_.user, config_.pass);

        auto conn = std::make_shared<pqxx::connection>(command.c_str());

        if (conn->is_open())
        {
            return std::make_shared<Database>(std::move(conn));
        }

        Message::CriticalMessage("Failed to open database connection.");
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Exception caught during database connection creation: {}", e.what()));
    }

    return nullptr;
}

DatabaseConnectionPool::DatabaseConnectionPool()
{
    try
    {
        for (uint16_t i = 0; i < config_.max_conn; ++i)
        {
            unsigned int retryCount            = 0;
            bool         connectionEstablished = false;

            while (retryCount < MAX_RETRIES && !connectionEstablished)
            {
                auto future = std::async(std::launch::async, &DatabaseConnectionPool::createDatabaseConnection, this);
                auto status = future.wait_for(std::chrono::seconds(TIMEOUT));

                if (status == std::future_status::ready)
                {
                    auto conn = future.get();
                    if (conn != nullptr)
                    {
                        databaseConnections.push(conn);
                        Message::InitMessage(fmt::format("Connection {}/{} created successfully.", i + 1, config_.max_conn));
                        connectionEstablished = true;
                        break;
                    }
                }

                retryCount++;
                if (retryCount < MAX_RETRIES)
                {
                    Message::WarningMessage(fmt::format("Connection attempt {} timed out, retrying... ({}/{})", i + 1, retryCount, MAX_RETRIES));
                    std::this_thread::sleep_for(std::chrono::seconds(1U << retryCount));
                }
            }

            if (!connectionEstablished)
            {
                while (!databaseConnections.empty())
                {
                    databaseConnections.pop();
                }

                Message::ErrorMessage(fmt::format("Failed to establish connection {} after {} attempts.", i + 1, MAX_RETRIES));
                throw std::runtime_error("Database connection pool initialization failure.");
            }
        }
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Failed to initialize database connection pool: {}", e.what()));
        std::exit(EXIT_FAILURE);  // NOLINT
    }
}

std::shared_ptr<Database> DatabaseConnectionPool::get_connection()
{
    std::unique_lock<std::mutex> lock(mutex);
    while (databaseConnections.empty())
    {
        cv.wait(lock);
    }
    auto db_ptr = databaseConnections.front();
    databaseConnections.pop();
    return db_ptr;
}

void DatabaseConnectionPool::return_connection(std::shared_ptr<Database> db_ptr)
{
    std::lock_guard<std::mutex> lock(mutex);
    databaseConnections.push(std::move(db_ptr));
    cv.notify_one();
}
