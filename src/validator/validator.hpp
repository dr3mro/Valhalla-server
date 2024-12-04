#pragma once

#include <algorithm>
#include <jsoncons/basic_json.hpp>
#include <unordered_map>

#include "utils/global/http.hpp"
#include "utils/global/types.hpp"
class Validator
{
   public:
    struct Rule
    {
        enum Action
        {
            NONE                             = 0,
            IGNORE_IF_MISSING_FROM_SCHEMA    = 1 << 0,  // Ignore if key is missing from database schema
            IGNORE_IF_NOT_NULLABLE_IN_SCHEMA = 1 << 1,  // Ignore if key is not nullable in database schema
            ASSERT_IMMUTABLE                 = 1 << 2,  // Assert that key is immutable
            ASSERT_NOT_PRESENT               = 1 << 3,  // Assert that key is not present in json
        };

        Action                                                      action;
        std::unordered_map<Action, std::unordered_set<std::string>> keys;

        bool check(const Action &check, const std::string &key) const
        {
            bool isMatch = hasMatch(check);
            bool isKey   = hasKey(check, key);
            return isMatch && isKey;
            // return hasMatch(check) && hasKey(check, key);
        }

        // constructor for rule
        explicit Rule(Action action, const std::unordered_set<std::string> &&keys)
        {
            this->action       = action;
            this->keys[action] = keys;
        }

        Rule() = delete;

        // Overload | operator
        friend Action operator|(Action a, Action b) { return static_cast<Action>(static_cast<int>(a) | static_cast<int>(b)); }

        // Overload & operator
        friend Action operator&(Action a, Action b) { return static_cast<Action>(static_cast<int>(a) & static_cast<int>(b)); }

        // Overload |= operator
        friend Action &operator|=(Action &a, Action b)
        {
            a = a | b;
            return a;
        }

        // Overload &= operator
        friend Action &operator&=(Action &a, Action b)
        {
            a = a & b;
            return a;
        }

        // Invert operator
        friend Action operator~(Action a) { return static_cast<Action>(~static_cast<int>(a)); }

        // Check if any flag is set
        explicit operator bool() const { return static_cast<int>(action) != 0; }

       private:
        // check if rule is set
        bool hasMatch(const Action &check) const { return (action & check) != NONE; }
        bool hasMatch(const Action &key, const Action &check) const { return (key & check) != NONE; }

        // check if key exists in rule for and action
        bool hasKey(const Action &check, const std::string &element) const
        {
            return std::ranges::find_if(this->keys, [&](const auto &key)
                                        { return (hasMatch(key.first, check)) && (key.second.contains(element)); }) != this->keys.end();
        }
    };

    Validator()          = default;
    virtual ~Validator() = default;
    static bool validateDatabaseCreateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error, const Rule &rule);
    static bool validateDatabaseUpdateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error, const Rule &rule);
    static bool validateDatabaseReadSchema(const std::unordered_set<std::string> &keys, const std::string &table_name, api::v2::Http::Error &error,
                                           const Rule &rule);
    static bool clientRegexValidation(const jsoncons::json &data, api::v2::Http::Error &error,
                                      std::unordered_set<std::pair<std::string, std::string>> &db_data);
    // Add this method declaration in the private section
    static bool hasDuplicateKeys(const jsoncons::json &data, api::v2::Http::Error &error);

   private:
    static bool                                    nullCheck(const jsoncons::json &data, api::v2::Http::Error &error);
    static std::unordered_set<api::v2::ColumnInfo> getDatabaseSchemaForTable(const std::string &tablename, api::v2::Http::Error &error, bool &found);
    static bool                                    validateType(const jsoncons::json &value, const std::string &expectedType);
    static bool checkColumns(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema, api::v2::Http::Error &error,
                             const Rule &rule);
    static bool ensureAllKeysExist(const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema,
                                   api::v2::Http::Error &error, const Rule &rule);
    static const std::unordered_map<std::string, std::string> regex_client_validators;
};
