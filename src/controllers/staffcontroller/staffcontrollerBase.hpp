
#pragma once

#include "crow/http_request.h"
#include <jsoncons/json.hpp>

using json = jsoncons::json;

class StaffControllerBase
{
   public:
    explicit StaffControllerBase() = default;
    virtual ~StaffControllerBase() = default;
    // CRUDS
    virtual void AddStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)      = 0;
    virtual void RemoveStaffFromEntity(const crow::request &req, crow::response &res, const jsoncons::json &body) = 0;
    virtual void InviteStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)   = 0;
};
