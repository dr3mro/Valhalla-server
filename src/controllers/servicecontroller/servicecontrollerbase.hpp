
#pragma once

#include <cstdint>
#include <jsoncons/json.hpp>

#include "crow.h"
/**
 * @brief Base class for service controllers that handle CRUD operations for a service.
 *
 * This abstract base class provides the interface for creating, reading, updating, deleting, and searching services.
 * Derived classes must implement the pure virtual functions to provide the specific implementation for a service.
 */
class ServiceControllerBase
{
   public:
    explicit ServiceControllerBase() = default;

    virtual ~ServiceControllerBase() = default;

    // CRUDS
    virtual void CreateService(const crow::request &req, crow::response &res, const jsoncons::json &body)        = 0;
    virtual void ReadService(const crow::request &req, crow::response &res, const jsoncons::json &criteria)      = 0;
    virtual void UpdateService(const crow::request &req, crow::response &res, const jsoncons::json &body)        = 0;
    virtual void DeleteService(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) = 0;
    virtual void SearchService(const crow::request &req, crow::response &res, const jsoncons::json &search_json) = 0;

   private:
    virtual std::optional<uint64_t> getNextID() = 0;
};