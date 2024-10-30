
#pragma once

#include <jsoncons/json.hpp>

#include "crow/http_request.h"
#include "crow/http_response.h"
#include "utils/resthelper/resthelper.hpp"

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
    virtual void Create(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void Read(const crow::request &req, crow::response &res, const json &request_json)   = 0;
    virtual void Update(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void Delete(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void Search(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void GetVisits(const crow::request &req, crow::response &res, const json &request_json)
    {
        (void)req;
        (void)res;
        (void)request_json;
        RestHelper::failureResponse(res, "Not implemented");
    };
};