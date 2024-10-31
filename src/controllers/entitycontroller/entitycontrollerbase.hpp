
#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

using json = jsoncons::json;
class EntityControllerBase
{
   public:
    explicit EntityControllerBase() = default;

    virtual ~EntityControllerBase() = default;

    // CRUDS
    virtual void Create(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void Read(const crow::request &req, crow::response &res, const json &request_json)   = 0;
    virtual void Update(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void Delete(const crow::request &req, crow::response &res, const json &request_json) = 0;
    virtual void Search(const crow::request &req, crow::response &res, const json &request_json) = 0;

   protected:
    virtual std::optional<uint64_t> getNextID() = 0;
};