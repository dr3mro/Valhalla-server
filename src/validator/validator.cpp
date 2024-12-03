#include "validator.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <regex>

#include "validator/databaseschema/databaseschema.hpp"

const std::unordered_map<std::string, std::string> Validator::regex_client_validators = {
    {"username", "^[a-z][a-z0-9_]*$"},
    {"password", "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[!@#$%^&*])[A-Za-z\\d!@#$%^&*]{8,}$"},
    {"phone", R"(^\+?(\d{1,3})?[-.\s]?(\(?\d{3}\)?)?[-.\s]?\d{3}[-.\s]?\d{4}$)"},
    {"email", R"((\w+)(\.\w+)*@(\w+)(\.\w+)+)"},
    {"dob", R"(^(0[1-9]|[12]\d|3[01])-(0[1-9]|1[0-2])-\d{4}$)"},
    {"gender", "^(Male|Female)$"},
};

bool Validator::validateDatabaseCreateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error, const Rule &rule)
{
    try
    {
        if (nullCheck(data, error))
        {
            return false;
        }
        // Get the schema for the table
        bool found        = false;
        auto table_schema = getDatabaseSchemaForTable(tablename, error, found);

        if (!found)
        {
            return false;
        }

        // now lets go through the data and check if the keys are in the schema
        // check for type and nullable

        if (!checkColumns(data, table_schema, error, rule))
        {
            return false;
        }

        // Ensure all keys in the schema are present in the data
        if (!ensureAllKeysExist(data, table_schema, error, rule))
        {
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error.message = e.what();
        error.code    = api::v2::Http::Status::INTERNAL_SERVER_ERROR;
        return false;
    }

    return true;
}

bool Validator::validateDatabaseUpdateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error, const Rule &rule)
{
    try
    {
        if (nullCheck(data, error))
        {
            return false;
        }
        // Get the schema for the table
        bool found        = false;
        auto table_schema = getDatabaseSchemaForTable(tablename, error, found);

        if (!found)
        {
            return false;
        }
        // Ensure all keys in the schema are present in the data
        // in case of update we dont need to check for all keys
        if (!ensureAllKeysExist(data, table_schema, error, rule))
        {
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error.message = e.what();
        error.code    = api::v2::Http::Status::INTERNAL_SERVER_ERROR;
        return false;
    }

    return true;
}

bool Validator::validateDatabaseReadSchema(const std::unordered_set<std::string> &keys, const std::string &table_name, api::v2::Http::Error &error,
                                           const Rule &rule)
{
    bool found = false;

    // Retrieve the schema for the given table
    auto columns = getDatabaseSchemaForTable(table_name, error, found);
    if (!found)
    {
        error.message = "Table not found";
        error.code    = api::v2::Http::Status::NOT_FOUND;
        return false;
    }

    // Check if all keys exist in the schema
    for (const auto &key : keys)
    {
        if (rule.check(Rule::Action::ASSERT_NOT_PRESENT, key))
        {
            error.message = "Key not allowed: " + key;
            error.code    = api::v2::Http::Status::BAD_REQUEST;
            return false;
        }
        // Use std::ranges::find_if to check if a column with the given name exists
        auto it = std::ranges::find_if(columns, [&](const api::v2::ColumnInfo &column) { return column.Name == key; });

        if (it == columns.end())
        {
            error.message = "Key not found in schema: " + key;
            error.code    = api::v2::Http::Status::BAD_REQUEST;
            return false;
        }
    }

    return true;
}
bool Validator::clientRegexValidation(const jsoncons::json &data, api::v2::Http::Error &error,
                                      std::unordered_set<std::pair<std::string, std::string>> &db_data)
{
    for (const auto &item : data.object_range())
    {
        std::optional<std::string> value = item.value().as<std::string>();
        if (value.has_value() && !value->empty())
        {
            auto pattern_item = std::ranges::find_if(regex_client_validators, [&](const auto &validator) { return validator.first == item.key(); });

            if (pattern_item != regex_client_validators.end())
            {
                std::regex pattern(pattern_item->second);
                if (!std::regex_match(value.value(), pattern))
                {
                    error = {.code    = api::v2::Http::Status::BAD_REQUEST,
                             .message = fmt::format("Key ({}) Value({}) is invalid.", item.key(), value.value())};
                    return false;
                }
            }
            db_data.insert({item.key(), value.value()});
        }
        else
        {
            error.message = "Value is empty for key " + item.key();
            error.code    = api::v2::Http::Status::BAD_REQUEST;
            return false;
        }
    }
    return true;
}

bool Validator::nullCheck(const jsoncons::json &data, api::v2::Http::Error &error)
{
    if (data.is_null() || data.empty())
    {
        error.message = "Data is empty";
        error.code    = api::v2::Http::Status::BAD_REQUEST;
        return true;
    }
    return false;
}

std::unordered_set<api::v2::ColumnInfo> Validator::getDatabaseSchemaForTable(const std::string &tablename, api::v2::Http::Error &error, bool &found)
{
    SCHEMA_t schema = DatabaseSchema::getDatabaseSchema();

    // Ensure the table schema exists
    auto table_schema_it = schema.find(tablename);
    if ((table_schema_it == schema.end()) || table_schema_it->second.empty())
    {
        error.message = "Table not found";
        error.code    = api::v2::Http::Status::BAD_REQUEST;
        found         = false;
    }
    found = true;
    return table_schema_it->second;
}
bool Validator::validateType(const jsoncons::json &value, const std::string &expectedType)
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
        return (value.is_object() || value.is_array());
    if (expectedType == "timestamp with time zone" || expectedType == "time without time zone" || expectedType == "date")
        return value.is_string();  // Assuming dates are strings
    return false;                  // Unknown type
}

bool Validator::checkColumns(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema, api::v2::Http::Error &error,
                             const Rule &rule)
{
    for (const auto &column : table_schema)
    {
        bool column_exists = data.contains(column.Name);

        // Check for missing non-nullable columns
        if (!column_exists && !column.isNullable && !rule.check(Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA, column.Name))
        {
            error.message = "Non-nullable column missing in data: " + column.Name;
            error.code    = api::v2::Http::Status::BAD_REQUEST;
            return false;
        }

        // Validate data type if column exists
        if (column_exists)
        {
            const auto &value = data.at(column.Name);
            if (!validateType(value, column.DataType))
            {
                error.message = fmt::format("Data type mismatch for column: {} ,expected: {} ", column.Name, column.DataType);
                error.code    = api::v2::Http::Status::BAD_REQUEST;
                return false;
            }
        }
    }
    return true;
}
bool Validator::ensureAllKeysExist(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema,
                                   api::v2::Http::Error &error, const Rule &rule)
{
    // Create a set of column names for fast key lookup
    std::unordered_set<std::string> schema_columns;
    for (const auto &column : table_schema) schema_columns.insert(column.Name);

    // Ensure all keys in `data` exist in the schema
    for (const auto &entry : data.object_range())
    {
        const auto &key = entry.key();

        // check if the key is in the exclude list
        if (rule.check(Rule::Action::ASSERT_IMMUTABLE, key))
        {
            error.message = "Key: [" + key + "] is not allowed to be changed";
            error.code    = api::v2::Http::Status::BAD_REQUEST;
            return false;
        }

        bool ignore_if_missing_from_database_schema = rule.check(Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA, key);
        if (schema_columns.find(key) == schema_columns.end() && !ignore_if_missing_from_database_schema)  // Key not in schema
        {
            error.message = "Key: [" + key + "] is not found in database schema";
            error.code    = api::v2::Http::Status::BAD_REQUEST;
            return false;
        }
    }
    return true;
}