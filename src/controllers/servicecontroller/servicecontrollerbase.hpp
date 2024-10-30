
#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

#include "utils/resthelper/resthelper.hpp"

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
        (void)request_json;
        RestHelper::failureResponse(res, "GetVisits is NOT implemented for this entity");
    };
};