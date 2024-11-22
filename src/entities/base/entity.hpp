#pragma once

#include <sys/types.h>

#include <cstdint>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/base.hpp"
#include "entities/base/types.hpp"
#include "fmt/format.h"
#include "jsoncons/basic_json.hpp"
#include "store/store.hpp"
#include "utils/message/message.hpp"

class Entity : public Base
{
   public:
    template <typename T>
    Entity(const T &_data, const std::string &_tablename) : tablename(_tablename), data(Types::EntityType(_data))
    {
    }
    // Entity(const std::string &_tablename) : tablename(_tablename) {}

    virtual ~Entity() override = default;

    std::optional<std::string> getSqlCreateStatement() override
    {
        std::optional<std::string> query;
        try
        {
            std::vector<std::string> keys_arr;
            std::vector<std::string> values_arr;

            jsoncons::json data_json = std::get<Types::Create_t>(data).data;
            data_json["id"]          = std::get<Types::Create_t>(data).id;

            for (auto &it : data_json.object_range())
            {
                keys_arr.push_back(it.key());
                values_arr.push_back(it.value().as<std::string>());
            }

            std::string columns = fmt::format("{}", fmt::join(keys_arr, ","));
            std::string values  = fmt::format("'{}'", fmt::join(values_arr, "','"));

            query = fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING id;", tablename, columns, values);
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
            auto user_id = std::get<Types::Read_t>(data).id;
            auto schema  = std::get<Types::Read_t>(data).data;

            std::string columns = schema.empty() ? "*" : fmt::format("{}", fmt::join(schema, ", "));

            query = fmt::format("SELECT {} FROM {}_safe WHERE id={} LIMIT 1;", columns, tablename, user_id);
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
            json payload = std::get<Types::Update_t>(data).data;

            uint64_t id = std::get<Types::Update_t>(data).id;

            std::string update_column_values;

            for (auto it = payload.object_range().begin(); it != payload.object_range().end(); ++it)
            {
                update_column_values.append(fmt::format(" {} = '{}' ", it->key(), it->value().as<std::string>()));
                if (std::next(it) != payload.object_range().end())
                {
                    update_column_values.append(",");
                }
            }

            query = fmt::format("UPDATE {} set {} WHERE id={} returning id;", tablename, update_column_values, id);
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
        uint64_t                   id;

        try
        {
            id = std::get<Types::Delete_t>(data).id;
            // Construct SQL query using {fmt} for parameterized query
            query = fmt::format("DELETE FROM {} where id={} returning id;", tablename, id);
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
            Types::Search_t searchdata = std::get<Types::Search_t>(getData());
            query = fmt::format("SELECT * FROM {}_safe WHERE {} ILIKE '%{}%' ORDER BY {} {} LIMIT {} OFFSET {};", tablename, searchdata.filter,
                                searchdata.keyword, searchdata.order_by, searchdata.direction, searchdata.limit + 1, searchdata.offset);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql search statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }

        return query;
    }

    const Types::EntityType &getData() const { return data; }

    std::string getGroupName() const  // ie. tablename
    {
        return tablename;
    }

    template <typename T>
    bool check_id_exists()
    {
        uint64_t id     = std::get<T>(getData()).id;
        auto     result = databaseController->checkItemExists(tablename, "id", std::to_string(id));
        return result.value_or(false);
    }

   protected:
    std::string                         tablename;
    std::shared_ptr<DatabaseController> databaseController = Store::getObject<DatabaseController>();

   private:
    const Types::EntityType data;
};
