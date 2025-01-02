#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

class Database;
class Configurator;

class DatabaseConnectionPool
{
   public:
    DatabaseConnectionPool();
    DatabaseConnectionPool(const DatabaseConnectionPool &)            = delete;
    DatabaseConnectionPool(DatabaseConnectionPool &&)                 = delete;
    DatabaseConnectionPool &operator=(const DatabaseConnectionPool &) = delete;
    DatabaseConnectionPool &operator=(DatabaseConnectionPool &&)      = delete;
    virtual ~DatabaseConnectionPool()                                 = default;

    [[nodiscard]] std::shared_ptr<Database> get_connection();
    void                                    return_connection(std::shared_ptr<Database> &&db_ptr);
    void                                    reconnect_all();

   private:
    std::shared_ptr<Database> createDatabaseConnection(const auto &config_);

    std::shared_ptr<Configurator>         configurator_;
    std::deque<std::shared_ptr<Database>> databaseConnections_;
    std::mutex                            mutex_;  // protects databaseConnections
    std::condition_variable               cv_;

    static constexpr std::uint16_t TIMEOUT     = 2;
    static constexpr unsigned int  MAX_RETRIES = 5;
};
