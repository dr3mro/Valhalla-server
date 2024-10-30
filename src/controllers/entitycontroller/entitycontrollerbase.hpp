
#pragma once

#include <cstdint>
#include <jsoncons/json.hpp>

#include "crow/http_request.h"
#include "crow/http_response.h"

class EntityControllerBase
{
   public:
    explicit EntityControllerBase() = default;

    virtual ~EntityControllerBase() = default;

    // CRUDS
    virtual void Create(const crow::request &req, crow::response &res, const jsoncons::json &body)        = 0;
    virtual void Read(const crow::request &req, crow::response &res, const jsoncons::json &criteria)      = 0;
    virtual void Update(const crow::request &req, crow::response &res, const jsoncons::json &body)        = 0;
    virtual void Delete(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) = 0;
    virtual void Search(const crow::request &req, crow::response &res, const jsoncons::json &search_json) = 0;

   protected:
    virtual std::optional<uint64_t> getNextID() = 0;
};