#include "databaseconnectionpool.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

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
#include "database/databasebusy.hpp"
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
        acquireTimeout_                            = std::chrono::milliseconds(config.acquire_timeout_ms);

        // Every server thread blocks on one connection while it queries, and the watchdog needs one more. A smaller pool
        // makes requests queue (and time out with 503) even though nothing is slow.
        const unsigned int server_threads = configurator_->get<Configurator::ServerConfig>().threads;
        if (config.max_conn < server_threads + 1U)
        {
            Message::WarningMessage(fmt::format("DB_MAX_CONN ({}) is below SERVER_THREADS + 1 ({}): requests may queue for a connection.", config.max_conn,
                server_threads + 1U));
        }

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
                pool_.release(conn);
                Message::InitMessage(fmt::format("Connection {}/{} created successfully.", pool_.idle(), config.max_conn));
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
        Message::CriticalMessage(fmt::format("Failed to initialize database connection pool: {}", what));
        Message::ErrorMessage("Fatal Error!\nExiting...");
        exit(EXIT_FAILURE); /*NOLINT*/
    }
}

std::shared_ptr<Database> DatabaseConnectionPool::get_connection()
{
    const auto timeout = acquireTimeout_;
    const auto started = std::chrono::steady_clock::now();

    std::shared_ptr<Database> db_ptr = pool_.acquire(timeout);

    const auto waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

    if (db_ptr == nullptr)
    {
        Message::CriticalMessage(fmt::format("Timeout ({} ms) while waiting for a database connection", timeout.count()));
        throw DatabaseBusy();
    }

    if (waited_ms >= SLOW_WAIT_WARNING_MS)
    {
        // At most one warning per second, so a saturated pool does not flood the log.
        const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::int64_t       last   = lastSlowWaitWarningMs_.load();
        if (now_ms - last >= 1000 && lastSlowWaitWarningMs_.compare_exchange_strong(last, now_ms))
        {
            Message::WarningMessage(fmt::format("Waited {} ms for a database connection: the pool is saturated (see DB_MAX_CONN).", waited_ms));
        }
    }

    return db_ptr;
}

void DatabaseConnectionPool::return_connection(std::shared_ptr<Database>&& db_ptr) { pool_.release(std::move(db_ptr)); }

void DatabaseConnectionPool::reconnect_all()
{
    const Configurator::DatabaseConfig& config = configurator_->get<Configurator::DatabaseConfig>();

    std::vector<std::future<void>> futures;
    futures.reserve(config.max_conn);

    // The pool lock is held until every reconnect finished, so no idle connection can be handed out (and used) while it
    // is being reconnected.
    pool_.forEachIdle(
        [&futures](std::shared_ptr<Database>& connection)
        {
            futures.push_back(std::async(std::launch::async,
                [&connection]()
                {
                    try
                    {
                        if (connection->reconnect())
                        {
                            Message::InfoMessage(fmt::format("Database connection {} is re-established successfully", static_cast<void*>(connection.get())));
                        }
                        else
                        {
                            Message::ErrorMessage(fmt::format("Database connection {} failed to be re-established", static_cast<void*>(connection.get())));
                        }
                    }
                    catch (const std::exception& e)
                    {
                        Message::CriticalMessage(fmt::format("Reconnect exception: {}", e.what()));
                    }
                }));
        },
        [&futures]()
        {
            for (auto& future : futures)
            {
                future.wait();
            }
        });
}
