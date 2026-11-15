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
    Entity(const Entity&)            = delete;
    Entity(Entity&&)                 = delete;
    Entity& operator=(const Entity&) = delete;
    Entity& operator=(Entity&&)      = delete;
    template <typename T>

    Entity(const T& _data, const std::string& _tablename) : tablename(_tablename), data(EntityType(_data))
    {
    }

    ~Entity() override = default;

    std::optional<std::string> getSqlCreateStatement(pqxx::params& params) override
    {
        Binder binder(params);
        return buildCreateStatement(binder);
    }

    std::optional<std::string> getSqlReadStatement(pqxx::params& params) override
    {
        Binder binder(params);
        return buildReadStatement(binder);
    }

    std::optional<std::string> getSqlUpdateStatement(pqxx::params& params) override
    {
        Binder binder(params);
        return buildUpdateStatement(binder);
    }

    std::optional<std::string> getSqlDeleteStatement(pqxx::params& params) override
    {
        Binder binder(params);
        return buildDeleteStatement(binder);
    }

    std::optional<std::string> getSqlSearchStatement(pqxx::params& params) override
    {
        Binder binder(params);
        return buildSearchStatement(binder);
    }

    [[nodiscard("Warning: You should never discard the returned object")]] const EntityType& getData() const { return data; }

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
        auto result = databaseController->checkItemExists(tablename, "id", std::to_string(_id.value()));
        return result.value_or(false);
    }

   protected:
    // The SQL statements: one builder per operation. Binder::bind(value) returns the placeholder for that value.
    template <typename Binder>
    std::optional<std::string> buildCreateStatement(Binder& binder)
    {
        try
        {
            std::vector<std::string> keys_arr;
            std::vector<std::string> values_arr;

            jsoncons::json data_json = std::get<Create_t>(data).get_data();
            // With an id given (e.g. a patient's health record shares the patient id) it is inserted. Without one the
            // column is left out, so the database assigns it, and any "id" the client sent is dropped.
            if (const auto create_id = std::get<Create_t>(data).get_id(); create_id.has_value())
            {
                data_json["id"] = create_id.value();
            }
            else
            {
                data_json.erase("id");
            }

            for (auto& iterator : data_json.object_range())
            {
                keys_arr.push_back(iterator.key());
                values_arr.push_back(binder.bind(iterator.value().template as<std::string>()));
            }

            return fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING *;", tablename, fmt::join(keys_arr, ","), fmt::join(values_arr, ","));
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql create statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

    template <typename Binder>
    std::optional<std::string> buildReadStatement(Binder& binder)
    {
        try
        {
            auto user_id = std::get<Read_t>(data).get_id();
            auto schema  = std::get<Read_t>(data).get_data();

            std::string columns = schema.empty() ? "*" : fmt::format("{}", fmt::join(schema, ", "));

            if (!user_id.has_value())
            {
                Message::ErrorMessage(fmt::format("Failed to read data from table {}. No id provided.", tablename));
                return std::nullopt;
            }
            return fmt::format("SELECT {} FROM {}_safe WHERE id={} LIMIT 1;", columns, tablename, binder.bind(user_id.value()));
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql read statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

    template <typename Binder>
    std::optional<std::string> buildUpdateStatement(Binder& binder)
    {
        try
        {
            jsoncons::json payload = std::get<Update_t>(data).get_data();

            auto _id = std::get<Update_t>(data).get_id();

            if (!_id.has_value())
            {
                Message::ErrorMessage(fmt::format("Failed to update data from table {}. No id provided.", tablename));
                return std::nullopt;
            }

            std::vector<std::string> set_clauses;

            for (auto it = payload.object_range().begin(); it != payload.object_range().end(); ++it)
            {
                set_clauses.push_back(fmt::format("{} = {}", it->key(), binder.bind(it->value().template as<std::string>())));
            }

            return fmt::format("UPDATE {} SET {} WHERE id={} RETURNING *;", tablename, fmt::join(set_clauses, ", "), binder.bind(_id.value()));
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql update statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

    template <typename Binder>
    std::optional<std::string> buildDeleteStatement(Binder& binder)
    {
        try
        {
            std::optional<uint64_t> _id = std::get<Delete_t>(data).get_id();
            if (!_id.has_value())
            {
                Message::ErrorMessage(fmt::format("Failed to delete data from table {}. No id provided.", tablename));
                return std::nullopt;
            }
            return fmt::format("DELETE FROM {} WHERE id={} RETURNING id;", tablename, binder.bind(_id.value()));
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql delete statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

    template <typename Binder>
    std::optional<std::string> buildSearchStatement(Binder& binder)
    {
        try
        {
            Search_t searchdata = std::get<Search_t>(getData());
            // Bound in a fixed order (argument evaluation order is unspecified).
            const std::string keyword_ph = binder.bind(fmt::format("%{}%", searchdata.keyword));
            const std::string limit_ph   = binder.bind(searchdata.limit + 1);
            const std::string offset_ph  = binder.bind(searchdata.offset);
            return fmt::format(
                "SELECT * FROM {}_safe WHERE {}::text ILIKE {} ORDER BY {} {} "
                "LIMIT {} OFFSET {};",
                tablename, searchdata.filter, keyword_ph, searchdata.order_by, searchdata.direction, limit_ph, offset_ph);
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create Sql search statement for table {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

    const std::string                   tablename;                                                   /*NOLINT*/
    std::shared_ptr<DatabaseController> databaseController = Store::getObject<DatabaseController>(); /*NOLINT*/

   private:
    const EntityType data;
};

// [ ] add UUID for all entities and make it autogenerated by PostgreSQL.
// [ ] some optional fields should be checked before get value from them.
