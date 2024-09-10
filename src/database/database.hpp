#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <fmt/format.h>

#include <jsoncons/json.hpp>
#include <pqxx/pqxx>  // Include the libpqxx header for PostgreSQL
using json = jsoncons::json;

/**
 * @class Database
 * @brief Provides an interface for interacting with a PostgreSQL database.
 *
 * The `Database` class is responsible for managing the connection to a PostgreSQL
 * database and executing SQL queries. It provides methods for checking if the
 * connection is established, checking if a table and column combination exists,
 * and executing both simple and complex queries.
 *
 * The `executeQuery` method allows executing arbitrary SQL queries and returns
 * the results as a JSON array. The `doSimpleQuery` method is a convenience
 * function for executing simple queries that return a single value.
 */
/**
 * @class Database
 * @brief Provides an interface for interacting with a PostgreSQL database.
 *
 * The `Database` class is responsible for managing the connection to a PostgreSQL
 * database and executing SQL queries. It provides methods for checking if the
 * connection is established, checking if a table and column combination exists,
 * and executing both simple and complex queries.
 *
 * The `executeQuery` method allows executing arbitrary SQL queries and returns
 * the results as a JSON array. The `doSimpleQuery` method is a convenience
 * function for executing simple queries that return a single value.
 */
/**
 * @class Database
 * @brief Provides an interface for interacting with a PostgreSQL database.
 *
 * The `Database` class is responsible for managing the connection to a PostgreSQL
 * database and executing SQL queries. It provides methods for checking if the
 * connection is established, checking if a table and column combination exists,
 * and executing both simple and complex queries.
 *
 * The `executeQuery` method allows executing arbitrary SQL queries and returns
 * the results as a JSON array. The `doSimpleQuery` method is a convenience
 * function for executing simple queries that return a single value.
 */
class Database
{
   public:
    Database(std::shared_ptr<pqxx::connection> conn);
    ~Database() = default;

    bool isConnected();
    bool checkExists(const std::string &table, const std::string &column, const std::string &value);

    template <typename jsonType, typename TransactionType>
    std::optional<jsonType> executeQuery(const std::string &query)
    {
        try
        {
            TransactionType txn(*connection);
            pqxx::result    res = txn.exec(query);

            if constexpr (std::is_same_v<TransactionType, pqxx::work>)
            {
                txn.commit();
            }

            jsonType reply;
            json     object;

            for (const auto &row : res)
            {
                for (const auto &field : row)
                {
                    std::string field_name = field.name();
                    int         field_type = field.type();

                    if (field.is_null())
                    {
                        object[field_name] = nullptr;
                    }
                    else
                    {
                        switch (field_type)
                        {
                            case 1043:  // TEXT or VARCHAR
                                object[field_name] = field.as<std::string>();
                                break;

                            case 23:  // INTEGER
                                object[field_name] = field.as<int>();
                                break;

                            case 16:  // BOOLEAN
                                object[field_name] = field.as<bool>();
                                break;

                            case 114:   // JSON
                            case 3802:  // JSONB
                                object[field_name] = jsoncons::json::parse(field.as<std::string>());
                                break;

                            default:                                 // Handle unknown or unhandled types
                                object[field_name] = field.c_str();  // Default to string representation
                                break;
                        }
                    }
                }
                if constexpr (std::is_same_v<jsonType, json::array>)
                {
                    reply.push_back(object);
                }
                else
                {
                    return object;
                }
            }
            return reply;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error executing query: " << e.what() << std::endl;
            throw;  // Rethrow the exception to indicate failure
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<T> doSimpleQuery(const std::string &query)
    {
        try
        {
            pqxx::nontransaction txn(*connection);
            pqxx::result         result = txn.exec(query);
            return result[0][0].as<std::optional<T>>();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error executing query: " << e.what() << std::endl;
            throw;  // Rethrow the exception to indicate failure
        }
    }

   private:
    std::shared_ptr<pqxx::connection> connection;
};

#endif  // DATABASE_HPP
