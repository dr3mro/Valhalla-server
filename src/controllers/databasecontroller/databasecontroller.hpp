#pragma once
#include <jsoncons/json.hpp>

#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"

using json = jsoncons::json;

/**
 * @class DatabaseController
 * @brief Provides an interface for executing database queries and performing common database operations.
 *
 * The DatabaseController class is responsible for managing database connections and executing various types of database
 * queries. It provides methods for executing read and write queries, checking if an item exists in a table, and
 * retrieving password hashes and user IDs.
 *
 * The class uses a DatabaseConnectionPool to manage database connections, ensuring efficient use of resources and
 * avoiding the overhead of repeatedly opening and closing connections.
 *
 * The executer() template method is a utility function that handles the execution of database queries, including error
 * handling and connection management.
 */
/**
 * A utility function that handles the execution of database queries, including error handling and connection
 * management.
 *
 * The `executer()` function is a template method that takes a callable `f` and its arguments `args`, and executes the
 * query using a database connection from the `DatabaseConnectionPool`. It returns an `std::optional<R>` containing the
 * result of the query, or `std::nullopt` if an exception occurs during execution.
 *
 * @tparam R The return type of the callable `f`.
 * @tparam F The type of the callable `f`.
 * @tparam Args The types of the arguments to the callable `f`.
 * @param f The callable to be executed.
 * @param args The arguments to be passed to the callable `f`.
 * @return An `std::optional<R>` containing the result of the query, or `std::nullopt` if an exception occurs.
 */
class DatabaseController
{
   public:
    DatabaseController();
    ~DatabaseController() = default;

    std::optional<json>        executeQuery(const std::string &query);
    std::optional<json>        executeReadQuery(const std::string &query);
    std::optional<json::array> executeSearchQuery(const std::string &query);
    std::optional<std::string> doReadQuery(const std::string &query);
    std::optional<bool>        checkItemExists(const std::string &table, const std::string &column, const std::string &value);
    std::optional<std::string> getPasswordHashForUserID(const uint64_t &user_id, const std::string &tablename);
    std::optional<uint64_t>    findIfUserID(const std::string &username, const std::string &tablename);  // check if user found and return 0 if not
                                                                                                         // found or return ID if found

   private:
    std::shared_ptr<DatabaseConnectionPool> databaseConnectionPool;

    template <typename R, typename F, typename... Args>
    /**
     * @brief A utility function that handles the execution of database queries, including error handling and connection
     * management.
     *
     * The `executer()` function is a template method that takes a callable `f` and its arguments `args`, and executes
     * the query using a database connection from the `DatabaseConnectionPool`. It returns an `std::optional<R>`
     * containing the result of the query, or `std::nullopt` if an exception occurs during execution.
     *
     * @tparam R The return type of the callable `f`.
     * @tparam F The type of the callable `f`.
     * @tparam Args The types of the arguments to the callable `f`.
     * @param f The callable to be executed.
     * @param args The arguments to be passed to the callable `f`.
     * @return An `std::optional<R>` containing the result of the query, or `std::nullopt` if an exception occurs.
     */
    std::optional<R> executer(const F &f, Args &&...args)
    {
        std::shared_ptr<Database> db = nullptr;
        try
        {
            db                       = databaseConnectionPool->get_connection();
            std::optional<R> results = std::invoke(f, db.get(), std::forward<Args>(args)...);
            //(db.get()->*f)(std::forward<Args>(args)...);
            databaseConnectionPool->return_connection(std::move(db));
            if (results)
            {
                return results.value();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception occurred during query execution: " << e.what() << std::endl;
            throw;
        }
        catch (...)
        {
            std::cerr << "Unknown exception occurred during query execution." << std::endl;
            throw;
        }
        return std::nullopt;
    }
};
