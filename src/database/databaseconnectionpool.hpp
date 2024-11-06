#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "configurator/configurator.hpp"
#include "database.hpp"
#include "store/store.hpp"

class DatabaseConnectionPool
{
   private:
    std::shared_ptr<Configurator>       configurator_ = Store::getObject<Configurator>();
    const Configurator::DatabaseConfig &config_       = configurator_->get<Configurator::DatabaseConfig>();

   public:
    DatabaseConnectionPool();
    std::shared_ptr<Database> get_connection();
    void                      return_connection(std::shared_ptr<Database> db);

   private:
    std::shared_ptr<Database>             createDatabaseConnection();
    std::queue<std::shared_ptr<Database>> databaseConnections;
    std::mutex                            mutex;
    std::condition_variable               cv;
};
