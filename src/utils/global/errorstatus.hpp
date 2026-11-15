#pragma once

#include <exception>

#include "database/databasebusy.hpp"
#include "utils/global/http.hpp"

namespace api::v2::Http
{
    // 503 when no database connection was available (the client should retry), 500 for any other failure.
    inline Status statusForException(const std::exception& e)
    {
        return dynamic_cast<const DatabaseBusy*>(&e) != nullptr ? Status::SERVICE_UNAVAILABLE : Status::INTERNAL_SERVER_ERROR;
    }
}  // namespace api::v2::Http
