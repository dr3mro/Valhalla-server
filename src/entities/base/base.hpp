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

    // Every builder returns SQL with $1, $2, ... placeholders and stores the values in params; values are never
    // written into the SQL text.
    virtual std::optional<std::string> getSqlCreateStatement(pqxx::params& params) = 0;
    virtual std::optional<std::string> getSqlReadStatement(pqxx::params& params)   = 0;
    virtual std::optional<std::string> getSqlUpdateStatement(pqxx::params& params) = 0;
    virtual std::optional<std::string> getSqlDeleteStatement(pqxx::params& params) = 0;
    virtual std::optional<std::string> getSqlSearchStatement(pqxx::params& params) = 0;

   protected:
    // A builder asks the binder how each value is written into the query: bind(value) stores it in params and
    // returns its placeholder ($1, $2, ...).
    struct Binder
    {
        explicit Binder(pqxx::params& p) : params(p) { params = {}; }

        template <typename T>
        std::string bind(const T& value)
        {
            params.append(value);
            return fmt::format("${}", ++index);
        }

       private:
        pqxx::params& params;
        size_t        index = 0;
    };
};
