#pragma once

#include <string>
#include <unordered_map>

#include "database/database.hpp"

#define SCHEMA_t std::unordered_map<std::string, std::vector<Database::ColumnInfo>>
class DatabaseSchema
{
   public:
    DatabaseSchema();
    virtual ~DatabaseSchema() = default;

    void            populateSchema(const std::string& tableName);
    static SCHEMA_t getDatabaseSchema();
    void            printSchema();

   private:
    static SCHEMA_t databaseSchema;
};