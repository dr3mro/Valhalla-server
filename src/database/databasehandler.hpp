#pragma once

#include <memory>

#include "database/databaseconnectionpool.hpp"
class DatabaseHanndler
{
   public:
    DatabaseHanndler();
    DatabaseHanndler(const DatabaseHanndler &)            = default;
    DatabaseHanndler(DatabaseHanndler &&)                 = delete;
    DatabaseHanndler &operator=(const DatabaseHanndler &) = default;
    DatabaseHanndler &operator=(DatabaseHanndler &&)      = delete;
    virtual ~DatabaseHanndler();

    std::shared_ptr<Database> get_connection();

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;
    std::shared_ptr<Database>               db_ptr;
};