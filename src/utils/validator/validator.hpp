#pragma once

#include <jsoncons/basic_json.hpp>

#include "utils/global/types.hpp"
class Validator
{
   public:
    Validator()          = default;
    virtual ~Validator() = default;
    static bool validateDatabaseSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Global::HttpError &error,
                                       const std::unordered_set<std::string> &exclude, bool isUpdate);
    static bool ensureAllKeysExist(const std::unordered_set<std::string> &keys, const std::string &table_name, api::v2::Global::HttpError &error);

    static bool clientValidationAndHashPasswd(const jsoncons::json &data, api::v2::Global::HttpError &error,
                                              std::unordered_set<std::pair<std::string, std::string>> &db_data);

   private:
    static bool                                    nullCheck(const jsoncons::json &data, api::v2::Global::HttpError &error);
    static std::unordered_set<api::v2::ColumnInfo> getDatabaseSchemaForTable(const std::string &tablename, api::v2::Global::HttpError &error,
                                                                             bool &found);
    static bool                                    validateType(const jsoncons::json &value, const std::string &expectedType);
    static bool checkColumns(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema,
                             const std::unordered_set<std::string> &exclude, api::v2::Global::HttpError &error);
    static bool ensureAllKeysExist(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema,
                                   api::v2::Global::HttpError &error, const std::unordered_set<std::string> &exclude);
    static const std::unordered_map<std::string, std::string> regex_client_validators;
};