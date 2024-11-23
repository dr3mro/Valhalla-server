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
    virtual ~DatabaseSchema() = default;

    void            populateSchema(const std::string& tableName);
    static SCHEMA_t getDatabaseSchema();
    void            printSchema();

   private:
    static SCHEMA_t databaseSchema;
};