#pragma once

#include <any>

#include "configurator/configurator.hpp"
#include "crow/utility.h"
#include "entities/base/base.hpp"
#include "entities/base/datatypes/clientdata.hpp"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "jsoncons/basic_json.hpp"
#include "store/store.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"
using json = jsoncons::json;

class Entity : public Base
{
   public:
    struct CreateData
    {
        json     data_json;
        uint64_t next_id;

        CreateData(const json &data, const uint64_t nid) : data_json(data), next_id(nid) {}

        CreateData() = default;
    };

    struct ReadData
    {
        std::vector<std::string> schema;  ///< The schema of the entity.
        uint64_t                 id;      ///< The ID of the entity to be read.

        /**
         * @brief Constructs a new ReadData instance.
         *
         * @param _schema The schema of the entity.
         * @param _id The ID of the entity to be read.
         */
        ReadData(const std::vector<std::string> &_schema, const uint64_t _id) : schema(_schema), id(_id) {}

        /**
         * @brief Default constructor for ReadData.
         */
        ReadData() = default;
    };

    /**
     * @brief Represents data for updating an entity.
     *
     * This struct contains the payload data and the user ID associated with the
     * update operation.
     */
    struct UpdateData
    {
        json     payload;  ///< The payload data for the update.
        uint64_t user_id;  ///< The user ID associated with the update.

        /**
         * @brief Constructs a new UpdateData instance.
         *
         * @param _data The payload data for the update.
         * @param id The user ID associated with the update.
         */
        UpdateData(const json &_data, const uint64_t id) : payload(_data), user_id(id) {}

        /**
         * @brief Default constructor for UpdateData.
         */
        UpdateData() = default;
    };

    /**
     * @brief Represents data for deleting an entity.
     *
     * This struct contains the payload data and the user ID associated with the
     * delete operation.
     *
     * @param payload The payload data for the delete operation.
     * @param user_id The user ID associated with the delete operation.
     */
    struct DeleteData
    {
        json     data_json;  ///< The payload data for the delete operation.
        uint64_t id;         ///< The user ID associated with the delete operation.

        /**
         * @brief Constructs a new DeleteData instance.
         *
         * @param _data The payload data for the delete operation.
         * @param id The user ID associated with the delete operation.
         */
        DeleteData(const json &data)
        {
            this->data_json = data;
            this->id        = data_json.find("id")->value().as<uint64_t>();
        }

        /**
         * @brief Default constructor for DeleteData.
         */
        DeleteData() = default;
    };

    /**
     * @brief Represents search data for querying an entity.
     *
     * This struct contains the search parameters, such as the keyword, order by
     * column, sort direction, limit, and offset, used to query an entity.
     *
     * @param keyword The search keyword.
     * @param filter The filter to apply to the search.
     * @param order_by The column to order the results by.
     * @param direction The sort direction, either "ASC" or "DESC".
     * @param limit The maximum number of results to return.
     * @param offset The number of results to skip.
     */
    using SearchData = struct SearchData
    {
        std::string keyword;
        std::string filter;
        std::string order_by;
        std::string direction;
        size_t      limit;
        size_t      offset;

        SearchData(const json &search_json, bool &success)
        {
            try
            {
                keyword   = search_json.at("keyword").as<std::string>();
                filter    = search_json.at("filter").as<std::string>();
                order_by  = search_json.at("order_by").as<std::string>();
                direction = search_json.at("direction").as<short>() == 0 ? "ASC" : "DESC";
                limit     = search_json.at("limit").as<size_t>();
                offset    = search_json.at("offset").as<size_t>();
            }
            catch (const std::exception &e)
            {
                success = false;
                throw std::runtime_error(std::string(e.what()));
            }
            success = true;
        }
        SearchData() = default;
    };

    /**
     * @brief Represents data for logging out a user.
     *
     * This struct contains an optional token value associated with the logout
     * operation.
     *
     * @param token The optional token value for the logout operation.
     */

    template <typename T>
    Entity(const T &_data, const std::string &_tablename) : tablename(_tablename), data(_data)
    {
    }
    Entity(const std::string &_tablename) : tablename(_tablename) {}

    ~Entity() override = default;

    std::optional<std::string> getSqlCreateStatement() override
    {
        std::optional<std::string> query;
        try
        {
            std::vector<std::string> keys_arr;
            std::vector<std::string> values_arr;

            json     data_json = std::any_cast<CreateData>(data).data_json;
            uint64_t next_id   = std::any_cast<CreateData>(data).next_id;
            data_json["id"]    = next_id;

            for (auto &it : data_json.object_range())
            {
                keys_arr.push_back(it.key());
                values_arr.push_back(it.value().as<std::string>());
            }

            std::string columns = fmt::format("{}", fmt::join(keys_arr, ","));
            std::string values  = fmt::format("'{}'", fmt::join(values_arr, "','"));

            query = fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING id;", tablename, columns, values);
            // std::cout << query.value() << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for create " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return query;
    };
    /**
     * Generates an SQL query for reading a record from the database.
     *
     * This method is part of the `Entity` class, which is responsible for
     * managing database interactions for a specific table. The
     * `getSqlReadStatement()` method generates an SQL `SELECT` query based on the
     * data provided in the `ReadData` struct.
     *
     * The method extracts the `id` and `schema` from the `ReadData` struct, and
     * constructs the SQL query with the appropriate column names and the `id`
     * value as the filter. The generated SQL query is returned as an optional
     * string, or `std::nullopt` if an exception occurs during the query
     * construction.
     *
     * @return An optional string containing the generated SQL query, or
     * `std::nullopt` if an exception occurs.
     */
    std::optional<std::string> getSqlReadStatement() override
    {
        std::optional<std::string> query;
        try
        {
            auto user_id = std::any_cast<ReadData>(data).id;
            auto schema  = std::any_cast<ReadData>(data).schema;

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
    /**
     * Generates an SQL query for updating a record in the database.
     *
     * This method is part of the `Entity` class, which is responsible for
     * managing database interactions for a specific table. The
     * `getSqlUpdateStatement()` method generates an SQL `UPDATE` query based on
     * the data provided in the `UpdateData` struct.
     *
     * The method extracts the `payload` and `user_id` from the `UpdateData`
     * struct, and constructs the SQL query with the appropriate column names and
     * values to be updated. The generated SQL query is returned as an optional
     * string, or `std::nullopt` if an exception occurs during the query
     * construction.
     *
     * @return An optional string containing the generated SQL query, or
     * `std::nullopt` if an exception occurs.
     */

    std::optional<std::string> getSqlUpdateStatement() override
    {
        std::optional<std::string> query;

        try
        {
            json payload = std::any_cast<UpdateData>(data).payload;

            uint64_t id = std::any_cast<UpdateData>(data).user_id;

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
    /**
     * Generates an SQL query for deleting a record from the database.
     *
     * This method is part of the `Entity` class, which is responsible for
     * managing database interactions for a specific table. The
     * `getSqlDeleteStatement()` method generates an SQL `DELETE` query based on
     * the data provided in the `DeleteData` struct.
     *
     * The method extracts the `id` from the `DeleteData` struct, and constructs
     * the SQL query to delete the record with the specified `id` from the table.
     * The generated SQL query is returned as an optional string, or
     * `std::nullopt` if an exception occurs during the query construction.
     *
     * @return An optional string containing the generated SQL query, or
     * `std::nullopt` if an exception occurs.
     */
    std::optional<std::string> getSqlDeleteStatement() override
    {
        std::optional<std::string> query;
        uint64_t                   id;

        try
        {
            id = std::any_cast<DeleteData>(data).id;
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
            SearchData searchdata = std::any_cast<SearchData>(getData());
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
    std::string getGroupName() const  // ie. tablename
    {
        return tablename;
    }

   protected:
    std::string tablename;

   private:
    std::any data;
};
