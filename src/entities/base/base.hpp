#pragma once

#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

/// Base class for all entities in the application.
/// Provides common functionality for creating, reading, updating, deleting, and
/// searching entities. Derived classes must implement the virtual functions to
/// provide the SQL statements for each operation.

class Base
{
   public:
    explicit Base() = default;
    virtual ~Base() = default;

    virtual std::optional<std::string> getSqlCreateStatement() = 0;
    virtual std::optional<std::string> getSqlReadStatement()   = 0;
    virtual std::optional<std::string> getSqlUpdateStatement() = 0;
    virtual std::optional<std::string> getSqlDeleteStatement() = 0;
    virtual std::optional<std::string> getSqlSearchStatement() = 0;

    const pqxx::params &getSqlParams() const { return sql_params_; }
    // pqxx::params keeps its values private, so the raw text of every bound value is mirrored here
    // and used to build the string that goes through the SQL-injection detector.
    const std::vector<std::string> &getSqlParamValues() const { return sql_param_values_; }

   protected:
    void resetSqlParams()
    {
        sql_params_ = {};
        sql_param_values_.clear();
    }
    void addSqlParam(const std::string &value)
    {
        sql_params_.append(value);
        sql_param_values_.push_back(value);
    }

    pqxx::params             sql_params_;
    std::vector<std::string> sql_param_values_;
};
