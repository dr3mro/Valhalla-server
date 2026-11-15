#pragma once

#include <stdexcept>
#include <string>

// Thrown when no database connection became free within DB_ACQUIRE_TIMEOUT_MS. It is not a query failure: callers answer
// 503 with Retry-After instead of a generic error.
class DatabaseBusy : public std::runtime_error
{
   public:
    explicit DatabaseBusy(const std::string& what = "Database is busy, no connection available") : std::runtime_error(what) {}
};
