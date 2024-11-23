#pragma once

#include <jsoncons/basic_json.hpp>

#include "utils/databaseschema/databaseschema.hpp"
#include "utils/global/types.hpp"
class Validator
{
   public:
    Validator()          = default;
    virtual ~Validator() = default;
    static bool validateDatabaseSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Global::HttpError &error,
                                       const std::unordered_set<std::string> &exclude);

   private:
    static bool                              nullCheck(const jsoncons::json &data, api::v2::Global::HttpError &error);
    static std::vector<Database::ColumnInfo> getDatabaseSchemaForTable(const std::string &tablename, api::v2::Global::HttpError &error, bool &found);
    static bool                              validateType(const jsoncons::json &value, const std::string &expectedType);
    static bool                              checkColumns(const jsoncons::json &data, const std::vector<Database::ColumnInfo> &table_schema,
                                                          const std::unordered_set<std::string> &exclude, api::v2::Global::HttpError &error);
    static bool                              ensureAllKeysExist(const jsoncons::json &data, const std::vector<Database::ColumnInfo> &table_schema,
                                                                api::v2::Global::HttpError &error, const std::unordered_set<std::string> &exclude);
};