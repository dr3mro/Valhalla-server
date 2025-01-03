#pragma once

#include <algorithm>
#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "utils/global/http.hpp"
#include "utils/global/types.hpp"
class Validator
{
   public:
    Validator()                                      = default;
    virtual ~Validator()                             = default;
    Validator(const Validator &other)                = default;
    Validator &operator=(const Validator &other)     = default;
    Validator(Validator &&other) noexcept            = default;
    Validator &operator=(Validator &&other) noexcept = default;

    struct Rule
    {
        enum Action : std::uint8_t
        {
            NONE                             = 0,
            IGNORE_IF_MISSING_FROM_SCHEMA    = 1U << 0U,  // Ignore if key is missing from database schema
            IGNORE_IF_NOT_NULLABLE_IN_SCHEMA = 1U << 1U,  // Ignore if key is not nullable in database schema
            ASSERT_IMMUTABLE                 = 1U << 2U,  // Assert that key is immutable
            ASSERT_NOT_PRESENT               = 1U << 3U,  // Assert that key is not present in json
        };

        Action                                                      action;  // NOLINT
        std::unordered_map<Action, std::unordered_set<std::string>> keys;    // NOLINT

        bool check(const Action &check, const std::string &key) const  // NOLINT
        {
            bool isMatch = hasMatch(check);
            bool isKey   = hasKey(check, key);
            return isMatch && isKey;
        }

        // constructor for rule
        explicit Rule(Action _action, const std::unordered_set<std::string> &&_keys) : action(_action) { this->keys[action] = _keys; }

        Rule() = delete;

        // Overload | operator
        friend Action operator|(Action first, Action second)
        {
            return static_cast<Action>(static_cast<std::uint8_t>(first) | static_cast<std::uint8_t>(second));
        }

        // Overload & operator
        friend Action operator&(Action first, Action second)
        {
            return static_cast<Action>(static_cast<std::uint8_t>(first) & static_cast<std::uint8_t>(second));
        }

        // Overload |= operator
        friend Action &operator|=(Action &first, Action second)
        {
            first = first | second;
            return first;
        }

        // Overload &= operator
        friend Action &operator&=(Action &first, Action second)
        {
            first = first & second;
            return first;
        }

        // Invert operator
        friend Action operator~(Action first) { return static_cast<Action>(~static_cast<std::uint8_t>(first)); }

        // Check if any flag is set
        explicit operator bool() const { return static_cast<int>(action) != 0; }

       private:
        // check if rule is set
        bool hasMatch(const Action &check) const { return (action & check) != NONE; }                  // NOLINT
        bool hasMatch(const Action &key, const Action &check) const { return (key & check) != NONE; }  // NOLINT

        // check if key exists in rule for and action
        bool hasKey(const Action &check, const std::string &element) const  // NOLINT
        {
            return std::ranges::find_if(this->keys, [&](const auto &key) { return (hasMatch(key.first, check)) && (key.second.contains(element)); }) !=
                   this->keys.end();
        }
    };

    static bool validateDatabaseCreateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error, const Rule &rule);
    static bool validateDatabaseUpdateSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Http::Error &error, const Rule &rule);
    static bool validateDatabaseReadSchema(
        const std::unordered_set<std::string> &keys, const std::string &table_name, api::v2::Http::Error &error, const Rule &rule);
    static bool clientRegexValidation(
        const jsoncons::json &data, api::v2::Http::Error &error, std::unordered_set<std::pair<std::string, std::string>> &db_data);

   private:
    static bool                                    nullCheck(const jsoncons::json &data, api::v2::Http::Error &error);
    static std::unordered_set<api::v2::ColumnInfo> getDatabaseSchemaForTable(const std::string &tablename, api::v2::Http::Error &error, bool &found);
    static bool                                    validateType(const jsoncons::json &value, const std::string &expectedType);
    static bool                                    checkColumns(
                                           const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema, api::v2::Http::Error &error, const Rule &rule);
    static bool ensureAllKeysExist(
        const jsoncons::json &data, const std::unordered_set<api::v2::ColumnInfo> &table_schema, api::v2::Http::Error &error, const Rule &rule);
    static bool hasDuplicateKeys(const jsoncons::json &data, api::v2::Http::Error &error);
};
