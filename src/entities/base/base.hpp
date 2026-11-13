#pragma once

#include <fmt/format.h>

#include <cstddef>
#include <optional>
#include <pqxx/pqxx>
#include <string>

/// Base class for all entities in the application.
/// Provides common functionality for creating, reading, updating, deleting, and
/// searching entities. Derived classes must implement the virtual functions to
/// provide the SQL statements for each operation.

class Base
{
   public:
    explicit Base() = default;
    virtual ~Base() = default;

    // Plain query (values inline): used only for the SQL-injection check, never executed.
    virtual std::optional<std::string> getSqlCreateStatement() = 0;
    // Parameterized query ($1, $2, ...): the one that is executed; the values go into params.
    virtual std::optional<std::string> getSqlCreateStatementParameterized(pqxx::params& params) = 0;
    // Plain query (values inline): used only for the SQL-injection check, never executed.
    virtual std::optional<std::string> getSqlReadStatement() = 0;
    // Parameterized query ($1, $2, ...): the one that is executed; the values go into params.
    virtual std::optional<std::string> getSqlReadStatementParameterized(pqxx::params& params) = 0;
    // Plain query (values inline): used only for the SQL-injection check, never executed.
    virtual std::optional<std::string> getSqlUpdateStatement() = 0;
    // Parameterized query ($1, $2, ...): the one that is executed; the values go into params.
    virtual std::optional<std::string> getSqlUpdateStatementParameterized(pqxx::params& params) = 0;
    // Plain query (values inline): used only for the SQL-injection check, never executed.
    virtual std::optional<std::string> getSqlDeleteStatement() = 0;
    // Parameterized query ($1, $2, ...): the one that is executed; the values go into params.
    virtual std::optional<std::string> getSqlDeleteStatementParameterized(pqxx::params& params) = 0;
    // Plain query (values inline): used only for the SQL-injection check, never executed.
    virtual std::optional<std::string> getSqlSearchStatement() = 0;
    // Parameterized query ($1, $2, ...): the one that is executed; the values go into params.
    virtual std::optional<std::string> getSqlSearchStatementParameterized(pqxx::params& params) = 0;

   protected:
    // The plain and the parameterized statement of an entity share ONE builder; the builder asks a "binder"
    // how each value is written into the query. Only the binder differs between the two variants.

    // Writes the value inline ('value'): the query is only scanned for SQL injection, never executed.
    struct PlainBinder
    {
        static std::string bind(const std::string& value) { return fmt::format("'{}'", value); }
    };

    // Writes a placeholder ($1, $2, ...) and stores the value in params: this is the query that is executed.
    struct ParamBinder
    {
        explicit ParamBinder(pqxx::params& p) : params(p) { params = {}; }
        std::string bind(const std::string& value)
        {
            params.append(value);
            return fmt::format("${}", ++index);
        }

       private:
        pqxx::params& params;
        size_t        index = 0;
    };
};
