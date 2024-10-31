
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
class AppointmentControllerBase
{
   public:
    explicit AppointmentControllerBase() = default;

    virtual ~AppointmentControllerBase() = default;

    // CRUDS
    virtual void Create(const crow::request &req, crow::response &res, const jsoncons::json &request_json) = 0;
    virtual void Read(const crow::request &req, crow::response &res, const jsoncons::json &request_json)   = 0;
    virtual void Update(const crow::request &req, crow::response &res, const jsoncons::json &request_json) = 0;
    virtual void Delete(const crow::request &req, crow::response &res, const jsoncons::json &request_json) = 0;
    virtual void Search(const crow::request &req, crow::response &res, const jsoncons::json &request_json) = 0;
};