/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file databaseschema.hpp
 * @brief Process-wide cache of the database schema used by the validators.
 * @details This header declares the DatabaseSchema class, which owns a single
 * shared map describing every table and its columns. The map is filled once at
 * server startup by asking DatabaseController for the table list and each
 * table's columns, and is read-only afterwards. It also defines SCHEMA_t, the
 * convenient name for that map's type, used throughout the validator code.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The validators need to know a table's columns and types to check client JSON,
 * and querying the database on every request would be far too slow. This class
 * provides that knowledge as a startup snapshot that Validator reads through
 * getDatabaseSchema(). Without it, the validators would have no schema to
 * compare against and would have to query the database on every create, update
 * and read request.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static data members / storage duration:
 * https://en.cppreference.com/w/cpp/language/storage_duration
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <string>  // For std::string, the table and column name type.
// For std::unordered_map, mapping a table name to its columns.
#include <unordered_map>
// For std::unordered_set, holding each table's unique columns.
#include <unordered_set>

// For api::v2::ColumnInfo, the description of one column.
#include "utils/global/types.hpp"

/** @brief Schema snapshot type: maps a table name to the set of its column
 * descriptions. */
#define SCHEMA_t      \
  std::unordered_map< \
      std::string,    \
      std::unordered_set<api::v2::ColumnInfo>>  // A readable alias for the
                                                // whole cached schema.
/**
 * @class DatabaseSchema
 * @brief Process-wide cache of the database schema used by the validators.
 *
 * @details The single static @ref databaseSchema map is populated at
 * construction by querying DatabaseController for every table and its columns,
 * and is thereafter read-only. The object is created eagerly by Server's
 * constructor and owned by Store, so the schema is available before any request
 * validation runs. Copy construction/assignment are allowed but share the same
 * static state; move operations are explicitly disabled.
 *
 * @note Because the schema is a startup snapshot, schema changes made
 * externally while the server is running are not reflected until restart.
 * @warning Construction terminates the process (or throws) when no
 * tables/columns can be read, so a reachable database is a startup
 * prerequisite.
 * @see Validator, DatabaseController, Store, SCHEMA_t
 */
class DatabaseSchema
{
 public:
  /**
   * @brief Eagerly loads the schema for every table reported by
   * DatabaseController.
   * @throws std::runtime_error if no tables are found.
   * @note There is no lazy fallback: a successful construction implies a
   * populated cache.
   */
  DatabaseSchema();
  /** @brief Copy constructor; shares the static schema map with other
   * instances. */
  DatabaseSchema(const DatabaseSchema&) = default;
  /** @brief Moving is disabled because the meaningful state is static. */
  DatabaseSchema(DatabaseSchema&&) = delete;
  /** @brief Copy assignment; shares the static schema map with other
   * instances. */
  DatabaseSchema& operator=(const DatabaseSchema&) = default;
  /** @brief Move assignment is disabled because the meaningful state is
   * static. */
  DatabaseSchema& operator=(DatabaseSchema&&) = delete;
  /** @brief Virtual destructor; the schema snapshot has no per-instance
   * resources. */
  virtual ~DatabaseSchema() = default;

  /**
   * @brief Loads (or refreshes) the column set for one table into the cache.
   * @param[in] tableName Table whose schema is fetched from
   * DatabaseController.
   * @note On a missing/empty schema a warning and error are logged and the
   * process exits; there is no recoverable error return.
   */
  static void populateSchema(const std::string& tableName);
  /**
   * @brief Returns the cached schema snapshot.
   * @return Const reference to the static map of table names to column sets.
   * @note Valid only after construction; the reference remains valid for the
   * process lifetime.
   */
  static const SCHEMA_t& getDatabaseSchema();
  /** @brief Logs every cached table/column tuple at info level (diagnostic
   * helper). */
  static void printSchema();

 private:
  /** @brief Shared schema snapshot; defined and populated once in
   * databaseschema.cpp. */
  static SCHEMA_t databaseSchema;  // One map shared by the whole program;
                                   // defined in the .cpp file.
};
