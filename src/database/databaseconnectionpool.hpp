/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file databaseconnectionpool.hpp
 * @brief Owns the fixed set of synchronous Database connections and leases them
 * to callers.
 * @details This header declares DatabaseConnectionPool, the manager for a fixed
 * number of Database objects. It builds every connection up front at startup,
 * stores idle ones in a BlockingPool, leases them with get_connection(), and
 * takes them back with return_connection(). It also exposes reconnect_all() to
 * refresh idle links after a database outage, and emits warnings when the pool
 * is saturated.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The pool is the gatekeeper for the server's synchronous database capacity.
 * Controllers and the watchdog do not create connections themselves;
 * DatabaseHanndler leases one from here for each request, and the HTTP layer
 * turns a timeout into 503 when no connection is free. The pool reads its size
 * and timeout from Configurator, and delegates the actual hand-out/return
 * mechanics to BlockingPool. Without it, every request would open its own
 * connection, which is slow, unbounded, and would exhaust the database's
 * connection limit.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (a lease is acquired in a constructor and returned in a destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::atomic (a value safe to read/write from several threads):
 * https://en.cppreference.com/w/cpp/atomic/atomic
 * - std::chrono (time points and durations):
 * https://en.cppreference.com/w/cpp/chrono
 * - std::shared_ptr (shared ownership of an object):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - Exception specifications via @throws (used by Doxygen, not C++ itself)
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: include this header at most once per translation
              // unit.

// std::atomic is used for the rate-limit timestamp written by several threads.
#include <atomic>
// std::chrono::milliseconds holds the configured acquire timeout.
#include <chrono>
// Fixed-width integers such as std::int64_t and std::uint16_t for the
// constants.
#include <cstdint>
// std::shared_ptr holds the Configurator and the leased Database objects.
#include <memory>

// BlockingPool<Database> is the thread-safe container of idle connections.
#include "database/blockingpool.hpp"

class Database;      // Forward declaration: only pointers are used here, so the
                     // full definition is not needed in this header.
class Configurator;  // Forward declaration: the pool stores a shared_ptr and
                     // calls it only in the .cpp file.

/**
 * @class DatabaseConnectionPool
 * @brief Owns the fixed set of synchronous Database connections and leases them
 * to callers.
 * @details Connections are created in parallel at construction and stored in a
 * BlockingPool. Callers obtain a connection with get_connection() and must give
 * it back with return_connection(); in practice DatabaseHanndler does this via
 * RAII. The acquire timeout is read once from configuration at startup. When a
 * wait for a connection expires, get_connection() throws DatabaseBusy, which
 * the HTTP layer maps to 503.
 *
 *          Thread-safety: connection hand-out and return are serialised by
 * BlockingPool; the slow-wait warning rate limiter is atomic. Non-copyable and
 * non-movable.
 * @warning A failed initialisation is fatal: the constructor logs and calls
 * `exit(EXIT_FAILURE)`.
 * @see BlockingPool, DatabaseHanndler, DatabaseBusy
 */
class DatabaseConnectionPool  // A fixed-size, thread-safe library of database
                              // connections shared by the whole server.
{
 public:
  /**
   * @brief Builds the pool from the Configurator, creating all connections up
   * front.
   * @details Reads the acquire timeout and `DB_MAX_CONN`, warns if the pool
   * is smaller than `SERVER_THREADS + 1`, then creates connections
   * concurrently with bounded retries and exponential backoff. Any creation
   * failure is fatal (process exit).
   */
  DatabaseConnectionPool();  // Pulls configuration from the Store and opens
                             // every connection before returning.
  DatabaseConnectionPool(const DatabaseConnectionPool&) =
      delete;  // Copying would duplicate the fixed set of live connections.
  DatabaseConnectionPool(DatabaseConnectionPool&&) =
      delete;  // Moving would let ownership escape the Store.
  DatabaseConnectionPool& operator=(const DatabaseConnectionPool&) =
      delete;  // Copy-assignment is meaningless for a live pool.
  DatabaseConnectionPool& operator=(DatabaseConnectionPool&&) =
      delete;  // Move-assignment is meaningless for a live pool.

  /**
   * @brief Destroys the pool. Connection cleanup is delegated to the members'
   * destructors.
   */
  virtual ~DatabaseConnectionPool() =
      default;  // The shared_ptr and BlockingPool members release their
                // resources automatically.

  // Waits up to DB_ACQUIRE_TIMEOUT_MS and throws DatabaseBusy when no
  // connection became free.
  /**
   * @brief Leases an idle connection, waiting at most the configured acquire
   * timeout.
   * @return A `shared_ptr` to a Database; the caller must eventually return
   * it.
   * @throws DatabaseBusy If no connection became free before the timeout.
   * @details Also emits a rate-limited warning when a connection took a long
   * time to acquire, so a saturated pool is visible in the logs.
   */
  [[nodiscard]] std::shared_ptr<Database>
  get_connection();  // [[nodiscard]] reminds callers that ignoring the leased
                     // pointer would leak a connection.

  /**
   * @brief Returns a previously leased connection to the idle set and wakes
   * one waiter.
   * @param[in,out] db_ptr Connection to return; a null pointer is ignored by
   * the pool.
   */
  void return_connection(std::shared_ptr<Database>&&
          db_ptr);  // An rvalue reference avoids a shared_ptr copy when
                    // handing the lease back.

  /**
   * @brief Reconnects every idle connection in parallel.
   * @details Holds the pool lock for the whole operation so no idle
   * connection can be handed out while it is being reconnected; busy
   * connections are not touched.
   */
  void reconnect_all();  // Called by the watchdog after it has repaired one
                         // connection.

 private:
  /**
   * @brief Creates and validates one Database from a configuration object.
   * @tparam Config_ Type exposing host/name/user/pass configuration fields.
   * @param[in] config_ Configuration used to build the connection string.
   * @return A valid Database, or `nullptr` if opening or validation failed.
   */
  std::shared_ptr<Database> createDatabaseConnection(
      const auto& config_);  // An abbreviated function template: the config
                             // type is deduced at the call site.

  std::shared_ptr<Configurator>
      configurator_;  // Keeps the application-wide settings object alive for
                      // the pool's lifetime.
  BlockingPool<Database>
      pool_;  // The actual thread-safe container of idle Database objects.
  std::chrono::milliseconds acquireTimeout_{
      0};  // DB_ACQUIRE_TIMEOUT_MS, read once at startup
  std::atomic<std::int64_t> lastSlowWaitWarningMs_{
      0};  // rate limits the slow-wait warning

  /**
   * Minimum wait (ms) that triggers a slow-acquire warning.
   */
  static constexpr std::int64_t SLOW_WAIT_WARNING_MS =
      100;  // Waits shorter than this are normal and stay silent in the logs.

  /**
   * libpq `connect_timeout` in seconds used when opening connections.
   */
  static constexpr std::uint16_t TIMEOUT =
      2;  // Prevents a dead database server from hanging startup
          // indefinitely.

  /**
   * Number of attempts to create a connection before giving up on that slot.
   */
  static constexpr unsigned int MAX_RETRIES =
      5;  // Combined with exponential backoff, this tolerates a brief startup
          // race with the DB.
};
