#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

class Database;
class Configurator;
class DatabaseConfig;

class DatabaseConnectionPool
{
   private:
    std::shared_ptr<Configurator> configurator_;

   public:
    DatabaseConnectionPool();
    DatabaseConnectionPool(const DatabaseConnectionPool &)            = delete;
    DatabaseConnectionPool(DatabaseConnectionPool &&)                 = delete;
    DatabaseConnectionPool &operator=(const DatabaseConnectionPool &) = delete;
    DatabaseConnectionPool &operator=(DatabaseConnectionPool &&)      = delete;

    virtual ~DatabaseConnectionPool() = default;

    [[nodiscard]] std::shared_ptr<Database> get_connection();
    void                                    return_connection(std::shared_ptr<Database> &&db_ptr);
    void                                    reconnect_all();

   private:
    std::shared_ptr<Database>             createDatabaseConnection(const auto &config_);
    std::deque<std::shared_ptr<Database>> databaseConnections;
    std::mutex                            mutex;
    std::condition_variable               cv;

    static constexpr std::uint16_t TIMEOUT     = 2;
    static constexpr unsigned int  MAX_RETRIES = 5;
};
