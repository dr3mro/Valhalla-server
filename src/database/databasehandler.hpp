/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file databasehandler.hpp
 * @brief RAII lease of a single pooled Database connection.
 * @details This header declares DatabaseHanndler (spelled exactly that way in
 * this codebase). Its constructor leases one connection from a
 * DatabaseConnectionPool, waiting up to the configured timeout, and its
 * destructor returns that connection. That makes it a small RAII guard: as long
 * as the handler object lives, the caller owns a valid database connection, and
 * it is released automatically on every exit path.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This class is the bridge between the connection pool and the code that
 * actually runs SQL. Controllers and the watchdog create one on the stack per
 * request or health check; they call get_connection() to reach the Database and
 * run queries, then simply let the handler go out of scope. Because returning
 * happens in the destructor, a thrown query exception cannot leak a connection.
 * Without this guard, callers would have to remember to return every lease by
 * hand, and one forgotten path would starve the pool.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (resource acquisition is initialisation):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::shared_ptr (shared ownership):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - std::move (transferring ownership instead of copying):
 * https://en.cppreference.com/w/cpp/utility/move
 * - Stack allocation and object lifetime:
 * https://en.cppreference.com/w/cpp/language/storage_duration
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: declare this class at most once per translation
              // unit.

// std::shared_ptr holds both the pool and the leased connection.
#include <memory>

class DatabaseConnectionPool;  // Forward declaration: only pointers are stored,
                               // so no full definition is needed here.
class Database;  // Forward declaration: the handler only passes around a
                 // shared_ptr to it.

/**
 * @class DatabaseHanndler
 * @brief RAII lease of a single pooled Database connection.
 * @details The constructor leases a connection from a DatabaseConnectionPool
 * (waiting up to the acquire timeout and possibly throwing DatabaseBusy); the
 * destructor returns it, so a thrown query exception cannot leak the
 * connection. Intended as a short-lived, stack-allocated helper (the watchdog
 * and controllers create one per check/query). Non-copyable and non-movable:
 * one lease, one connection.
 * @note The class name is spelled `DatabaseHanndler` as used throughout the
 * codebase.
 * @see DatabaseConnectionPool, DatabaseBusy
 */
class DatabaseHanndler  // A short-lived guard that owns exactly one connection
                        // borrowed from the pool.
{
 public:
  /**
   * @brief Leases a connection using the pool obtained from the Store.
   * @details Convenience constructor for rare callers (e.g. the watchdog)
   * that do not already hold the pool. Delegates to the shared_ptr-taking
   * constructor.
   * @throws DatabaseBusy If no connection becomes free before the acquire
   * timeout.
   */
  DatabaseHanndler();  // looks the pool up in the Store (rare callers such as
                       // the watchdog)

  /**
   * @brief Leases a connection from an already-held pool (hot path).
   * @param[in] pool Pool that owns connections; kept alive for the lease's
   * lifetime.
   * @throws DatabaseBusy If no connection becomes free before the acquire
   * timeout.
   */
  explicit DatabaseHanndler(std::shared_ptr<DatabaseConnectionPool>
                                pool);  // hot path: the caller already holds
                                        // the pool, no Store lookup
  DatabaseHanndler(const DatabaseHanndler&) =
      delete;  // Copying would double-return the same lease to the pool.
  DatabaseHanndler(DatabaseHanndler&&) =
      delete;  // Moving would let a lease exist without a clear owner.
  DatabaseHanndler& operator=(const DatabaseHanndler&) =
      delete;  // Copy-assignment would violate one-lease-one-owner.
  DatabaseHanndler& operator=(DatabaseHanndler&&) =
      delete;  // Move-assignment would violate one-lease-one-owner.

  /**
   * @brief Destroys the lease, returning the connection to the pool if one is
   * held.
   */
  virtual ~DatabaseHanndler();  // Virtual for safe polymorphic destruction;
                                // the body performs the automatic return.

  /**
   * @brief Returns the leased connection.
   * @return The `shared_ptr` borrowed from the pool; valid until this handler
   * is destroyed.
   */
  std::shared_ptr<Database>
  get_connection();  // Gives read-only-style access to the borrowed
                     // connection without transferring ownership.

  /**
   * @brief Forwards a reconnect request to the owning pool (reconnects idle
   * connections).
   * @see DatabaseConnectionPool::reconnect_all
   */
  void reconnect_all();  // Lets the watchdog ask the pool to refresh all idle
                         // links after a repair.

 private:
  /**
   * Pool that owns the leased connection; kept alive for the lifetime of the
   * lease.
   */
  std::shared_ptr<DatabaseConnectionPool>
      databaseConnectionPool;  // Holding the pool here guarantees it outlives
                               // the lease returned in the destructor.

  /**
   * The single connection leased in the constructor and returned in the
   * destructor.
   */
  std::shared_ptr<Database>
      db_ptr;  // The actual lease; non-null for the handler's whole lifetime
               // unless moved out.
};
