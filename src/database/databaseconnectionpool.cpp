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
#include <vector>

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
        Message::CriticalMessage(fmt::format("Failed to create a Database connection: {}", e.what()));
    }
    return nullptr;
}

DatabaseConnectionPool::DatabaseConnectionPool() : configurator_(Store::getObject<Configurator>())
{
    try
    {
        const Configurator::DatabaseConfig& config = configurator_->get<Configurator::DatabaseConfig>();

        std::vector<std::future<std::shared_ptr<Database>>> futures;

        futures.reserve(config.max_conn);
        for (uint16_t i = 0; i < config.max_conn; ++i)
        {
            futures.push_back(std::async(std::launch::async,
                [this, config]() -> std::shared_ptr<Database>
                {
                    unsigned int retryCount = 0;
                    while (retryCount < MAX_RETRIES)
                    {
                        auto conn = createDatabaseConnection(config);
                        if (conn)
                        {
                            return conn;
                        }
                        std::this_thread::sleep_for(std::chrono::seconds(1U << retryCount));
                        Message::WarningMessage(fmt::format("Failed to create a database connection. Attempt {}/{}...", ++retryCount, MAX_RETRIES));
                    }
                    return nullptr;
                }));
        }

        // Wait for all futures
        for (auto& future : futures)
        {
            auto conn = future.get();  // Blocks only on completion of the individual task
            if (conn != nullptr)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                databaseConnections_.push_back(conn);
                Message::InitMessage(fmt::format("Connection {}/{} created successfully.", databaseConnections_.size(), config.max_conn));
            }
            else
            {
                Message::ErrorMessage("Failed to establish one or more connections after maximum retries.");
                throw std::runtime_error("Database connection pool initialization failure.");
            }
        }
    }
    catch (const std::exception& e)
    {
        std::string what = e.what();
        Message::CriticalMessage(fmt::format("Failed to initialize database connection pool: {}", what.substr(0, -4)));
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
                Message::ErrorMessage(fmt::format("Database connection id: {} link failed to re-establish", static_cast<void*>(connection->get())));
            }
        }
        catch (const std::exception& e)
        {
            Message::CriticalMessage(fmt::format("Reconnect exception: {}", e.what()));
        }

        connection++;
    }
}
