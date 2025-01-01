#include "database/connectionhandler.hpp"

#include <memory>
#include <utility>

#include "database/databaseconnectionpool.hpp"

ConnectionHanndler::ConnectionHanndler(std::shared_ptr<DatabaseConnectionPool> &dbPool) : databaseConnectionPool(dbPool)
{
    db_ptr = databaseConnectionPool->get_connection();
}

ConnectionHanndler::~ConnectionHanndler()
{
    if (db_ptr != nullptr)
    {
        databaseConnectionPool->return_connection(std::move(db_ptr));
    }
}

std::shared_ptr<Database> ConnectionHanndler::get_connection() { return databaseConnectionPool->get_connection(); }
