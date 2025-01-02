#include "databaseconnectionpool.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "configurator/configurator.hpp"
#include "database.hpp"
#include "database/database.hpp"
#include "store/store.hpp"
#include "utils/message/message.hpp"

std::shared_ptr<Database> DatabaseConnectionPool::createDatabaseConnection(const auto& config)
{
    try
    {
        auto conn = std::make_shared<pqxx::connection>(
            fmt::format("host={} dbname={} user={} password={} connect_timeout={}", config.host, config.name, config.user, config.pass, TIMEOUT).c_str());

        if (conn->is_open())
        {
            return std::make_shared<Database>(std::move(conn));
        }

        Message::CriticalMessage("Failed to open database connection.");
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Database connection initialization failure : {}", e.what()));
    }
    return nullptr;
}

DatabaseConnectionPool::DatabaseConnectionPool() : configurator_(Store::getObject<Configurator>())
{
    try
    {
        const Configurator::DatabaseConfig& config = configurator_->get<Configurator::DatabaseConfig>();

        for (uint16_t i = 0; i < config.max_conn; ++i)
        {
            unsigned int retryCount            = 0;
            bool         connectionEstablished = false;

            while (retryCount < MAX_RETRIES && !connectionEstablished)
            {
                auto future = std::async(std::launch::async, [this, &config]() { return this->createDatabaseConnection(config); });
                auto status = future.wait_for(std::chrono::seconds(TIMEOUT + 1));

                if (status == std::future_status::ready)
                {
                    auto conn = future.get();
                    if (conn != nullptr)
                    {
                        databaseConnections_.push_back(conn);
                        Message::InitMessage(fmt::format("Connection {}/{} created successfully.", i + 1, config.max_conn));
                        connectionEstablished = true;
                        break;
                    }
                }

                retryCount++;

                Message::WarningMessage(fmt::format("Connection attempt {} timed out, retrying... ({}/{})", i + 1, retryCount, MAX_RETRIES));

                if (retryCount < MAX_RETRIES)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1U << retryCount));
                }
            }

            if (!connectionEstablished)
            {
                while (!databaseConnections_.empty())
                {
                    databaseConnections_.pop_front();
                }

                Message::ErrorMessage(fmt::format("Failed to establish connection {} after {} attempts.", i + 1, MAX_RETRIES));
                throw std::runtime_error("Database connection pool initialization failure.");
            }
        }
    }
    catch (const std::exception& e)
    {
        Message::CriticalMessage(fmt::format("Failed to initialize database connection pool: {}", e.what()));
        Message::ErrorMessage("Exiting...");
        exit(EXIT_FAILURE); /*NOLINT*/
    }
}

std::shared_ptr<Database> DatabaseConnectionPool::get_connection()
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (!cv_.wait_for(lock, std::chrono::seconds(1), [this] { return !databaseConnections_.empty(); }))
    {
        Message::CriticalMessage("Timeout while waiting for a connection");
        return nullptr;
    }

    std::shared_ptr<Database> db_ptr = std::move(databaseConnections_.front());

    databaseConnections_.pop_front();

    return db_ptr;
}

void DatabaseConnectionPool::return_connection(std::shared_ptr<Database>&& db_ptr)
{
    if (db_ptr != nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        databaseConnections_.push_back(std::move(db_ptr));
        cv_.notify_one();
    }
}

void DatabaseConnectionPool::reconnect_all()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto                        connection = databaseConnections_.begin();
    while (connection != databaseConnections_.end())
    {
        try
        {
            if ((*connection)->reconnect())
            {
                Message::InfoMessage(fmt::format("Database connection id: {} link is re-established", static_cast<void*>(connection->get())));
            }
            else
            {
                Message::ErrorMessage(fmt::format("Failed to reconnect to database {}", static_cast<void*>(connection->get())));
            }
        }
        catch (const std::exception& e)
        {
            Message::CriticalMessage(fmt::format("Reconnect exception: {}", e.what()));
        }

        connection++;
    }
}