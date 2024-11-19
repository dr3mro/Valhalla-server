
#pragma once

#include <drogon/drogon.h>

#include <jsoncons/json.hpp>

using json = jsoncons::json;
class EntityControllerBase
{
   public:
    explicit EntityControllerBase() = default;

    virtual ~EntityControllerBase() = default;

    // CRUDS
    virtual void Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) = 0;
    // virtual void Read(const crow::request& req, crow::response& res, const json& request_json)                                     = 0;
    // virtual void Update(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;
    // virtual void Delete(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params) = 0;
    // virtual void Search(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;
};