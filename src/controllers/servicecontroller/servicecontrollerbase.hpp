
#pragma once

#include <jsoncons/json.hpp>

#include "crow/http_request.h"
#include "crow/http_response.h"

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
    virtual void Create(const crow::request &req, crow::response &res, const jsoncons::json &body)        = 0;
    virtual void Read(const crow::request &req, crow::response &res, const jsoncons::json &criteria)      = 0;
    virtual void Update(const crow::request &req, crow::response &res, const jsoncons::json &body)        = 0;
    virtual void Delete(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) = 0;
    virtual void Search(const crow::request &req, crow::response &res, const jsoncons::json &search_json) = 0;
    virtual void GetVisits(const crow::request &req, crow::response &res, const jsoncons::json &criteria)
    {
        (void)req;
        (void)res;
        (void)criteria;
        throw std::runtime_error("Not implemented");
    };
};