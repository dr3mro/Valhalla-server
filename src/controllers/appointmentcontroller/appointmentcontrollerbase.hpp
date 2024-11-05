
#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

using json = jsoncons::json;
class AppointmentControllerBase
{
   public:
    explicit AppointmentControllerBase() = default;

    virtual ~AppointmentControllerBase() = default;

    // CRUDS
    virtual void Create(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;
    virtual void Read(const crow::request& req, crow::response& res, const json& request_json)                                     = 0;
    virtual void Update(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;
    virtual void Delete(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params) = 0;
    virtual void Search(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;
};