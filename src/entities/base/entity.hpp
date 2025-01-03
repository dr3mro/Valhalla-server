#pragma once

#include <fmt/core.h>
#include <sys/types.h>

#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/base.hpp"
#include "entities/base/types.hpp"
#include "fmt/format.h"
#include "jsoncons/basic_json.hpp"
#include "store/store.hpp"
#include "utils/message/message.hpp"

using EntityType = api::v2::Types::EntityType;
using Create_t   = api::v2::Types::Create_t;
using Read_t     = api::v2::Types::Read_t;
using Update_t   = api::v2::Types::Update_t;
using Delete_t   = api::v2::Types::Delete_t;
using Search_t   = api::v2::Types::Search_t;

class Entity : public Base
{
   public:
    Entity(const Entity &)            = delete;
    Entity(Entity &&)                 = delete;
    Entity &operator=(const Entity &) = delete;
    Entity &operator=(Entity &&)      = delete;
    template <typename T>

    Entity(const T &_data, const std::string &_tablename) : tablename(_tablename), data(EntityType(_data))
    {
    }

    ~Entity() override = default;

    std::optional<std::string> getSqlCreateStatement() override
    {
        std::optional<std::string> query;
        try
        {
            std::vector<std::string> keys_arr;
            std::vector<std::string> values_arr;

            jsoncons::json data_json = std::get<Create_t>(data).get_data();
            data_json["id"]          = std::get<Create_t>(data).get_id();

            for (auto &iterator : data_json.object_range())
            {
                keys_arr.push_back(iterator.key());
                values_arr.push_back(iterator.value().as<std::string>());
            }

            std::string columns = fmt::format("{}", fmt::join(keys_arr, ","));
            std::string values  = fmt::format("'{}'", fmt::join(values_arr, "','"));

            query = fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING *;", tablename, columns, values);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql create statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
        return query;
    };

    std::optional<std::string> getSqlReadStatement() override
    {
        std::optional<std::string> query;
        try
        {
            auto user_id = std::get<Read_t>(data).get_id();
            auto schema  = std::get<Read_t>(data).get_data();

            std::string columns = schema.empty() ? "*" : fmt::format("{}", fmt::join(schema, ", "));

            if (!user_id.has_value())
            {
                Message::ErrorMessage(fmt::format("Failed to read data from table {}. No id provided.", tablename));
            }
            query = fmt::format("SELECT {} FROM {}_safe WHERE id={} LIMIT 1;", columns, tablename, user_id.value());
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql read statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlUpdateStatement() override
    {
        std::optional<std::string> query;

        try
        {
            jsoncons::json payload = std::get<Update_t>(data).get_data();

            auto _id = std::get<Update_t>(data).get_id();

            if (!_id.has_value())
            {
                Message::ErrorMessage(fmt::format("Failed to update data from table {}. No id provided.", tablename));
            }

            std::string update_column_values;

            for (auto it = payload.object_range().begin(); it != payload.object_range().end(); ++it)
            {
                update_column_values.append(fmt::format(" {} = '{}' ", it->key(), it->value().as<std::string>()));
                if (std::next(it) != payload.object_range().end())
                {
                    update_column_values.append(",");
                }
            }

            query = fmt::format("UPDATE {} set {} WHERE id={} returning *;", tablename, update_column_values, _id.value());
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql update statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlDeleteStatement() override
    {
        std::optional<std::string> query;
        std::optional<uint64_t>    _id;

        try
        {
            _id = std::get<Delete_t>(data).get_id();
            if (!_id.has_value())
            {
                Message::ErrorMessage(fmt::format("Failed to delete data from table {}. No id provided.", tablename));
                return std::nullopt;
            }
            // Construct SQL query using {fmt} for parameterized query
            query = fmt::format("DELETE FROM {} where id={} returning id;", tablename, _id.value());
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql delete statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlSearchStatement() override
    {
        std::optional<std::string> query;
        try
        {
            Search_t searchdata = std::get<Search_t>(getData());
            query               = fmt::format(
                "SELECT * FROM {}_safe WHERE {}::text ILIKE '%{}%' ORDER BY {} {} "
                              "LIMIT {} OFFSET {};",
                tablename, searchdata.filter, searchdata.keyword, searchdata.order_by, searchdata.direction, searchdata.limit + 1, searchdata.offset);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql search statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }

        return query;
    }

    [[nodiscard("Warning: You should never discard the returned object")]] const EntityType &getData() const { return data; }

    [[nodiscard("Warning: You should never discard the returned object")]] std::string getGroupName() const  // ie. tablename
    {
        return tablename;
    }

    template <typename T>
    bool check_id_exists()
    {
        std::optional<uint64_t> _id = std::get<T>(getData()).get_id();
        if (!_id.has_value())
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql search statement for table {}.", tablename));
            return false;
        }
        bool isSqlInjection = false;
        auto result         = databaseController->checkItemExists(tablename, "id", std::to_string(_id.value()), isSqlInjection);

        if (isSqlInjection)
        {
            Message::ErrorMessage("A Sql Injection pattern is detected in generated query.");
            return false;
        }
        return result.value_or(false);
    }

   protected:
    const std::string                   tablename;                                                   /*NOLINT*/
    std::shared_ptr<DatabaseController> databaseController = Store::getObject<DatabaseController>(); /*NOLINT*/

   private:
    const EntityType data;
};

// [ ] add UUID for all entities and make it autogenerated by PostgreSQL.
// [ ] some optional fields should be checked before get value from them.
