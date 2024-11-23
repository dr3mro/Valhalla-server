#include "validator.hpp"

#include "store/store.hpp"
#include "utils/databaseschema/databaseschema.hpp"
bool Validator::validateDatabaseSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Global::HttpError &error,
                                       const std::unordered_set<std::string> &exclude)
{
    try
    {
        if (data.is_null() || data.empty())
        {
            error.message = "Data is empty";
            error.code    = 400;
            return false;
        }

        auto     db_schema = Store::getObject<DatabaseSchema>();
        SCHEMA_t schema    = db_schema->getDatabaseSchema();

        // Ensure the table schema exists
        auto table_schema_it = schema.find(tablename);
        if (table_schema_it == schema.end() || table_schema_it->second.empty())
        {
            error.message = "Table not found";
            error.code    = 400;
            return false;
        }

        const auto &table_schema = table_schema_it->second;

        // Create a set of column names for fast key lookup
        std::unordered_set<std::string> schema_columns;
        for (const auto &column : table_schema) schema_columns.insert(column.Name);

        // Lambda for type validation
        auto validateType = [](const jsoncons::json &value, const std::string &expectedType) -> bool
        {
            if (expectedType == "integer")
                return value.is_int64();
            if (expectedType == "float" || expectedType == "double" || expectedType == "real")
                return value.is_double();
            if (expectedType == "character varying")
                return value.is_string();
            if (expectedType == "boolean")
                return value.is_bool();
            if (expectedType == "jsonb")
                return value.is_object();
            if (expectedType == "timestamp with time zone" || expectedType == "time without time zone" || expectedType == "data")
                return value.is_string();  // Assuming dates are strings
            return false;                  // Unknown type
        };

        // Validate each column in the schema
        for (const auto &column : table_schema)
        {
            bool column_exists = data.contains(column.Name);

            // Check for missing non-nullable columns
            if (!column.isNullable && !column_exists && !exclude.contains(column.Name))
            {
                error.message = "Non-nullable column missing in data: " + column.Name;
                error.code    = 400;
                return false;
            }

            // Validate data type if column exists
            if (column_exists)
            {
                const auto &value = data.at(column.Name);
                if (!validateType(value, column.DataType))
                {
                    error.message = "Data type mismatch for column: " + column.Name;
                    error.code    = 400;
                    return false;
                }
            }
        }

        // Ensure all keys in `data` exist in the schema
        for (const auto &entry : data.object_range())
        {
            const auto &key = entry.key();
            if (schema_columns.find(key) == schema_columns.end())  // Key not in schema
            {
                error.message = "Key not found in schema: " + key;
                error.code    = 400;
                return false;
            }
        }
    }
    catch (const std::exception &e)
    {
        error.message = e.what();
        error.code    = 500;
        return false;
    }

    return true;
}