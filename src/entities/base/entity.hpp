#pragma once

#include <any>
#include <cstdint>
#include <regex>
#include <utility>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/base.hpp"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "jsoncons/basic_json.hpp"
#include "store/store.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"
#include "utils/resthelper/resthelper.hpp"
using json = jsoncons::json;

class Entity : public Base
{
   public:
    using Create_t = struct Create_t
    {
       public:
        jsoncons::json data_j;
        uint64_t       id;
        Create_t(const json &data, const uint64_t id) : data_j(data), id(id) {}
    };

    using Read_t = struct Read_t
    {
        std::vector<std::string> schema;
        uint64_t                 id;
        Read_t(const std::vector<std::string> &_schema, const uint64_t _id) : schema(_schema), id(_id) {}
    };

    using Update_t = struct Update_t
    {
       public:
        jsoncons::json data_j;
        uint64_t       id;
        Update_t(const json &data, const uint64_t id) : data_j(data), id(id) {}
    };

    using Delete_t = struct Delete_t
    {
        uint64_t id;
        Delete_t(const uint64_t _id) : id(_id) {}
    };

    using Search_t = struct Search_t
    {
        std::string keyword;
        std::string filter;
        std::string order_by;
        std::string direction;
        size_t      limit;
        size_t      offset;

        Search_t(const json &search_j, bool &success)
        {
            try
            {
                keyword   = search_j.at("keyword").as<std::string>();
                filter    = search_j.at("filter").as<std::string>();
                order_by  = search_j.at("order_by").as<std::string>();
                direction = search_j.at("direction").as<short>() == 0 ? "ASC" : "DESC";
                limit     = search_j.at("limit").as<size_t>();
                offset    = search_j.at("offset").as<size_t>();
            }
            catch (const std::exception &e)
            {
                success = false;
                throw std::runtime_error(std::string(e.what()));
            }
            success = true;
        }
    };

    template <typename T>
    Entity(const T &_data, const std::string &_tablename) : tablename(_tablename), data(_data)
    {
    }
    Entity(const std::string &_tablename) : tablename(_tablename) {}

    virtual ~Entity() override = default;

    std::optional<std::string> getSqlCreateStatement() override
    {
        std::optional<std::string> query;
        try
        {
            std::vector<std::string> keys_arr;
            std::vector<std::string> values_arr;

            json     data_json = std::any_cast<Create_t>(data).data_j;
            uint64_t next_id   = std::any_cast<Create_t>(data).id;
            data_json["id"]    = next_id;

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
            std::cerr << "faild to create query for create " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return query;
    };

    std::optional<std::string> getSqlReadStatement() override
    {
        std::optional<std::string> query;
        try
        {
            auto user_id = std::any_cast<Read_t>(data).id;
            auto schema  = std::any_cast<Read_t>(data).schema;

            std::string columns = schema.empty() ? "*" : fmt::format("{}", fmt::join(schema, ", "));

            query = fmt::format("SELECT {} FROM {}_safe WHERE id={} LIMIT 1;", columns, tablename, user_id);
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for read " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlUpdateStatement() override
    {
        std::optional<std::string> query;

        try
        {
            json payload = std::any_cast<Update_t>(data).data_j;

            uint64_t id = std::any_cast<Update_t>(data).id;

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
            std::cerr << "faild to create query for update " << tablename << e.what() << '\n';
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
            id = std::any_cast<Delete_t>(data).id;
            // Construct SQL query using {fmt} for parameterized query
            query = fmt::format("DELETE FROM {} where id={} returning id;", tablename, id);
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for delete " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlSearchStatement() override
    {
        std::optional<std::string> query;
        try
        {
            Search_t searchdata = std::any_cast<Search_t>(getData());
            query = fmt::format("SELECT * FROM {}_safe WHERE {} ILIKE '%{}%' ORDER BY {} {} LIMIT {} OFFSET {};", tablename, searchdata.filter,
                                searchdata.keyword, searchdata.order_by, searchdata.direction, searchdata.limit + 1, searchdata.offset);
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for search " << tablename << e.what() << '\n';
            return std::nullopt;
        }

        return query;
    }

    /**
     * Gets the data associated with this entity.
     *
     * This method returns the data associated with the current entity instance.
     * The data is stored in a `std::any` object, which can hold any type of data.
     *
     * @return The data associated with this entity.
     */
    std::any getData() const { return data; }
    /**
     * Gets the name of the table associated with this entity.
     *
     * This method returns the name of the database table that this entity
     * represents.
     *
     * @return The name of the table associated with this entity.
     */
    //   TODO:
    std::string getGroupName() const  // ie. tablename
    {
        return tablename;
    }

    template <typename T>
    bool check_id_exists()
    {
        uint64_t id     = std::any_cast<T>(getData()).id;
        auto     result = databaseController->checkItemExists(tablename, "id", std::to_string(id));
        return result.value_or(false);
    }

   protected:
    std::string                         tablename;
    std::shared_ptr<DatabaseController> databaseController = Store::getObject<DatabaseController>();
    std::shared_ptr<PasswordCrypt>      passwordCrypt      = Store::getObject<PasswordCrypt>();

   private:
    std::any data;
};
