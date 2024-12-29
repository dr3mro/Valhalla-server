#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "utils/global/types.hpp"

#define SCHEMA_t std::unordered_map<std::string, std::unordered_set<api::v2::ColumnInfo>>
class DatabaseSchema
{
   public:
    DatabaseSchema();
    DatabaseSchema(const DatabaseSchema&)            = default;
    DatabaseSchema(DatabaseSchema&&)                 = delete;
    DatabaseSchema& operator=(const DatabaseSchema&) = default;
    DatabaseSchema& operator=(DatabaseSchema&&)      = delete;
    virtual ~DatabaseSchema()                        = default;

    static void            populateSchema(const std::string& tableName);
    static const SCHEMA_t& getDatabaseSchema();
    static void            printSchema();

   private:
    static SCHEMA_t databaseSchema;
};