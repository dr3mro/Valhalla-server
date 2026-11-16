/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file databasehandler.cpp
 * @brief Implementation for databasehandler: Delegates to the pool-taking
 * constructor after resolving the pool from the Store.
 * @details This file implements the small RAII lease class. The default
 * constructor finds the shared DatabaseConnectionPool in the Store and hands it
 * to the pool-taking constructor, which immediately leases a connection. The
 * destructor returns that connection, and get_connection()/reconnect_all()
 * simply expose the lease or forward a request to the pool.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the implementation behind every synchronous database call: controllers
 * and the watchdog create a DatabaseHanndler, and these few functions are what
 * actually acquire and release the connection. Because the return to the pool
 * happens in the destructor, exceptions in query code cannot strand a
 * connection. Without this file the lease abstraction would be declared but
 * undefined, so no synchronous request could obtain a connection.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (acquire in constructor, release in destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - Delegating constructors (one constructor calls another):
 * https://en.cppreference.com/w/cpp/language/initializer_list
 * - std::move (transfer ownership without copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - std::shared_ptr: https://en.cppreference.com/w/cpp/memory/shared_ptr
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The DatabaseHanndler declaration implemented here.
#include "database/databasehandler.hpp"

// std::shared_ptr and std::move used for the pool and the lease.
#include <memory>
#include <utility>  // std::move is formally declared here.

// Full definition of DatabaseConnectionPool, needed to call its methods.
#include "database/databaseconnectionpool.hpp"
// Store::getObject<> locates the shared pool for the default constructor.
#include "store/store.hpp"

/**
 * @brief Delegates to the pool-taking constructor after resolving the pool from
 * the Store.
 */
DatabaseHanndler::DatabaseHanndler()
    : DatabaseHanndler(Store::getObject<DatabaseConnectionPool>())
{
}  // Delegating constructor: find the global pool once, then let the other
   // constructor do the leasing.

/**
 * Leases one connection immediately so the handler always owns a valid lease.
 * pool: Pool to lease from; stored so the destructor can return the connection.
 * Throws DatabaseBusy: If the pool's acquire timeout expires.
 */
DatabaseHanndler::DatabaseHanndler(std::shared_ptr<DatabaseConnectionPool> pool)
    : databaseConnectionPool(std::move(pool))
{
  db_ptr = databaseConnectionPool->get_connection();
}  // Store the pool, then lease a connection immediately; if none is free this
   // throws, so a half-built handler never exists.

/**
 * @brief Returns the leased connection to the pool, if any is still held.
 * @details The null check makes destruction safe even after a connection was
 * moved out.
 */
DatabaseHanndler::~DatabaseHanndler()
{
  if (db_ptr != nullptr)  // Only return a real lease; a null pointer means it
                          // was already moved away.
  {
    databaseConnectionPool->return_connection(
        std::move(db_ptr));  // Hand the connection back, waking one waiter
                             // in the pool.
  }
}

/**
 * @brief Returns the borrowed connection without transferring ownership.
 * @return The connection leased in the constructor.
 */
std::shared_ptr<Database> DatabaseHanndler::get_connection()
{
  return db_ptr;
}  // A copy of the shared_ptr: the handler keeps its own reference until
   // destruction.

/**
 * @brief Forwards the reconnect request to the owning pool.
 */
void DatabaseHanndler::reconnect_all()
{
  databaseConnectionPool->reconnect_all();
}  // The handler does not own the reconnect logic; the pool does.
