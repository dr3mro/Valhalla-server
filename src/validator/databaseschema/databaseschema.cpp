#include "databaseschema.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "store/store.hpp"
#include "utils/message/message.hpp"

SCHEMA_t DatabaseSchema::databaseSchema;

DatabaseSchema::DatabaseSchema()
{
    auto dbctl  = Store::getObject<DatabaseController>();
    auto tables = dbctl->getAllTables();

    if (!tables.has_value() || tables->empty())
    {
        throw std::runtime_error("No tables found in database.\n");
        Message::ErrorMessage("Exiting...");
        exit(EXIT_FAILURE); /*NOLINT*/
    }

    for (const auto& table : tables.value())
    {
        populateSchema(table);
    }
    // printSchema();
}

void DatabaseSchema::populateSchema(const std::string& tableName)
{
    auto dbctl  = Store::getObject<DatabaseController>();
    auto schema = dbctl->getTableSchema(tableName);
    if (schema.has_value())
    {
        databaseSchema[tableName] = dbctl->getTableSchema(tableName).value();
    }
    else
    {
        Message::WarningMessage(fmt::format("Table not found.\n", tableName));
        Message::ErrorMessage("Exiting...");
        exit(EXIT_FAILURE); /*NOLINT*/
    }
}

const SCHEMA_t& DatabaseSchema::getDatabaseSchema() { return databaseSchema; }

void DatabaseSchema::printSchema()
{
    for (const auto& [tableName, columns] : databaseSchema)
    {
        for (const auto& column : columns)
        {
            fmt::print("{} : {} - {} - {} - {} \n", tableName, column.Name, column.DataType, column.Constraint, column.isNullable);
        }
    }
}