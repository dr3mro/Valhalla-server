#include "database/asyncdberror.hpp"

#include <drogon/orm/Exception.h>

#include <string>

namespace asyncdb
{
    bool isBusy(const std::exception& error)
    {
        if (dynamic_cast<const DatabaseBusy*>(&error) != nullptr)
        {
            return true;
        }

        const std::string message = error.what();
        return message.find("timeout") != std::string::npos || message.find("pool") != std::string::npos ||
               message.find("too many clients") != std::string::npos || message.find("connection") != std::string::npos;
    }

    drogon::HttpStatusCode toHttpStatus(const std::exception& error)
    {
        if (isBusy(error))
        {
            return drogon::k503ServiceUnavailable;
        }
        return drogon::k500InternalServerError;
    }

    void throwIfBusy(const std::exception& error)
    {
        if (isBusy(error))
        {
            throw DatabaseBusy();
        }
    }
}  // namespace asyncdb
