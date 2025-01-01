#pragma once

#include <memory>

#include "database/databaseconnectionpool.hpp"
class ConnectionHanndler
{
   public:
    explicit ConnectionHanndler(std::shared_ptr<DatabaseConnectionPool> &dbPool);
    ConnectionHanndler()                                      = delete;
    ConnectionHanndler(const ConnectionHanndler &)            = default;
    ConnectionHanndler(ConnectionHanndler &&)                 = delete;
    ConnectionHanndler &operator=(const ConnectionHanndler &) = default;
    ConnectionHanndler &operator=(ConnectionHanndler &&)      = delete;
    virtual ~ConnectionHanndler();

    std::shared_ptr<Database> get_connection();

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;
    std::shared_ptr<Database>               db_ptr;
};