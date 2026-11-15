#pragma once

#include <memory>

class DatabaseConnectionPool;
class Database;

class DatabaseHanndler
{
   public:
    DatabaseHanndler();
    DatabaseHanndler(const DatabaseHanndler&)            = delete;
    DatabaseHanndler(DatabaseHanndler&&)                 = delete;
    DatabaseHanndler& operator=(const DatabaseHanndler&) = delete;
    DatabaseHanndler& operator=(DatabaseHanndler&&)      = delete;
    virtual ~DatabaseHanndler();

    std::shared_ptr<Database> get_connection();
    void                      reconnect_all();

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;
    std::shared_ptr<Database>               db_ptr;
};