#pragma once

#include <drogon/HttpTypes.h>
#include <drogon/orm/Exception.h>

#include <exception>
#include <string>

#include "database/databasebusy.hpp"

// Maps exceptions raised by the Drogon async database path to the HTTP status
// codes the synchronous path produces: an exhausted pool / timeout becomes 503
// (DatabaseBusy), any other failure becomes 500.
namespace asyncdb
{
    [[nodiscard]] bool isBusy(const std::exception& error);

    [[nodiscard]] drogon::HttpStatusCode toHttpStatus(const std::exception& error);

    // Throw DatabaseBusy when `error` represents an exhausted pool or timeout.
    void throwIfBusy(const std::exception& error);
}  // namespace asyncdb

namespace AsyncDbError = asyncdb;
