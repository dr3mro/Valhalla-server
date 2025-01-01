#include "database/databasehandler.hpp"

#include <memory>
#include <utility>

#include "database/databaseconnectionpool.hpp"
#include "store/store.hpp"

DatabaseHanndler::DatabaseHanndler() : databaseConnectionPool(Store::getObject<DatabaseConnectionPool>()) { db_ptr = databaseConnectionPool->get_connection(); }

DatabaseHanndler::~DatabaseHanndler()
{
    if (db_ptr != nullptr)
    {
        databaseConnectionPool->return_connection(std::move(db_ptr));
    }
}

std::shared_ptr<Database> DatabaseHanndler::get_connection() { return db_ptr; }
