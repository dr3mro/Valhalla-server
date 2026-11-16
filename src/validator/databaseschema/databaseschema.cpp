/**
 * @file databaseschema.cpp
 * @brief Implementation of the process-wide database schema cache declared in
 * databaseschema.hpp.
 * @details This file defines the single shared schema map and the functions
 * that manage it: the constructor that fills it for every table at startup,
 * populateSchema() that loads one table's columns, getDatabaseSchema() that
 * hands out a read-only view of the map, and printSchema() that logs its
 * contents. Failures while reading the schema are treated as fatal at startup.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the schema snapshot that Validator relies on is actually built.
 * Server's constructor creates a DatabaseSchema object, which uses
 * DatabaseController to discover tables and columns and stores them here.
 * Without this file the cache would be empty, so validation could not run and
 * every request would need a live database schema query.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static data members / storage duration:
 * https://en.cppreference.com/w/cpp/language/storage_duration
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Bring in the DatabaseSchema class, SCHEMA_t and the declarations we define.
#include "databaseschema.hpp"

#include <fmt/core.h>  // For fmt::format used to build log messages.
// Additional fmt formatting support used by the library headers.
#include <fmt/format.h>

// For EXIT_FAILURE and exit(), used on fatal startup errors.
#include <cstdlib>
#include <stdexcept>  // For std::runtime_error thrown when no tables are found.
#include <string>     // For std::string operations on table names.

// For DatabaseController::getAllTables/getTableSchema.
#include "controllers/databasecontroller/databasecontroller.hpp"
// For Store::getObject, which supplies the shared DatabaseController.
#include "store/store.hpp"
// For logging helpers like ErrorMessage and WarningMessage.
#include "utils/message/message.hpp"

SCHEMA_t
    DatabaseSchema::databaseSchema;  // Define the one shared schema map (empty
                                     // until the constructor fills it).

/**
 * See DatabaseSchema::DatabaseSchema.
 * Retrieves DatabaseController from Store, asks it for all table names, and
 * calls populateSchema() for each. A missing or empty table list is a fatal
 * startup condition. Note: The exit(EXIT_FAILURE) / "Exiting..." statements
 * after the throw are unreachable; the throw is the active error path.
 */
DatabaseSchema::DatabaseSchema()
{
  auto dbctl =
      Store::getObject<DatabaseController>();  // Fetch the shared controller
                                               // that can query the database.
  auto tables = dbctl->getAllTables();  // Ask it for the list of table names
                                        // (an optional that may be empty).

  if (!tables.has_value() ||
      tables->empty())  // No table list at all, or an empty list, is a fatal
                        // startup error.
  {
    throw std::runtime_error(
        "No tables found in database.\n");  // Report the failure as an
                                            // exception to the caller.
    Message::ErrorMessage("Exiting...");    // Unreachable: the throw above
                                            // already left this function.
    exit(EXIT_FAILURE);
    /*NOLINT*/  // Unreachable fallback that would end the process on
                // failure.
  }

  for (const auto& table :
       tables.value())  // Visit each table name that was returned.
  {
    populateSchema(table);  // Load that table's columns into the shared map.
  }
  // printSchema();
}

/**
 * See DatabaseSchema::populateSchema.
 * Fetches the column descriptions through DatabaseController and stores them
 * under
 * @p tableName in the static map. Failure is treated as fatal: an error is
 * logged and exit(EXIT_FAILURE) is called rather than leaving a partially
 * populated cache.
 */
void DatabaseSchema::populateSchema(const std::string& tableName)
{
  auto dbctl =
      Store::getObject<DatabaseController>();  // Get the controller used to
                                               // query this table's schema.
  auto schema = dbctl->getTableSchema(
      tableName);          // Ask for the columns of this table (an optional).
  if (schema.has_value())  // The query succeeded and produced column
                           // descriptions.
  {
    databaseSchema[tableName] = dbctl->getTableSchema(tableName)
                                    .value();  // Store the columns under the
                                               // table name in the shared map.
  }
  else  // The table's schema could not be read.
  {
    Message::WarningMessage(
        fmt::format("Table not found.\n",
                    tableName));  // Log a warning naming the problem.
    Message::ErrorMessage(
        "Exiting...");  // Log that startup is being abandoned.
    exit(EXIT_FAILURE);
    /*NOLINT*/  // End the process rather than keep a half-filled cache.
  }
}

/**
 * See DatabaseSchema::getDatabaseSchema.
 */
const SCHEMA_t& DatabaseSchema::getDatabaseSchema()
{
  return databaseSchema;
}  // Hand out a read-only reference to the shared map, no copy needed.

/**
 * See DatabaseSchema::printSchema.
 * Emits one info-level line per column, including table name, column name, data
 * type, constraint and nullability. Read-only diagnostic helper.
 */
void DatabaseSchema::printSchema()
{
  for (const auto& [tableName, columns] :
       databaseSchema)  // Walk every (table name, column set) pair in the
                        // cache.
  {
    for (const auto& column :
         columns)  // Walk every column description inside that table.
    {
      Message::InfoMessage(
          fmt::format("{} : {} - {} - {} - {} \n",
                      tableName,
                      column.Name,
                      column.DataType,
                      column.Constraint,
                      column
                          .isNullable));  // Log one line describing the column.
    }
  }
}
