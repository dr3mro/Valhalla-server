#pragma once

#include <jsoncons/basic_json.hpp>

#include "utils/global/http.hpp"
#include "utils/global/types.hpp"
class Validator
{
   public:
    Validator()          = default;
    virtual ~Validator() = default;
    static bool validateDatabaseCreateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error);
    static bool validateDatabaseUpdateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error,
                                             const std::unordered_set<std::string> &exclude);
    static bool validateDatabaseReadSchema(const std::unordered_set<std::string> &keys, const std::string &table_name, api::v2::Http::Error &error);
    static bool clientRegexValidation(const jsoncons::json &data, api::v2::Http::Error &error,
                                      std::unordered_set<std::pair<std::string, std::string>> &db_data);

   private:
    static bool                                    nullCheck(const jsoncons::json &data, api::v2::Http::Error &error);
    static std::unordered_set<api::v2::ColumnInfo> getDatabaseSchemaForTable(const std::string &tablename, api::v2::Http::Error &error, bool &found);
    static bool                                    validateType(const jsoncons::json &value, const std::string &expectedType);
    static bool checkColumns(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema, api::v2::Http::Error &error,
                             const std::unordered_set<std::string> &exclude = {});
    static bool ensureAllKeysExist(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema,
                                   api::v2::Http::Error &error, const std::unordered_set<std::string> &exclude = {});
    static const std::unordered_map<std::string, std::string> regex_client_validators;
};