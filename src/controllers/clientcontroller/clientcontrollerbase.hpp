#pragma once

#include <crow.h>
#include <stdint.h>

#include <jsoncons/json.hpp>
#include <optional>

using json = jsoncons::json;
class ClientControllerBase
{
   public:
    ClientControllerBase() = default;

    virtual ~ClientControllerBase() = default;

    // PUBLIC
    virtual void                    Create(const crow::request& req, crow::response& res, const json& criteria)                    = 0;
    virtual std::optional<uint64_t> Authenticate(const crow::request& req, crow::response& res, const jsoncons::json& credentials) = 0;
    virtual void                    Read(const crow::request& req, crow::response& res, const json& criteria)                      = 0;
    virtual void                    Update(const crow::request& req, crow::response& res, const json& criteria)                    = 0;
    virtual void                    Delete(const crow::request& req, crow::response& res, const json& delete_json)                 = 0;
    virtual void                    Search(const crow::request& req, crow::response& res, const json& search_json)                 = 0;
    virtual void                    Logout(const crow::request& req, crow::response& res, const std::optional<std::string>& token) = 0;
    virtual void                    Suspend(const crow::request& req, crow::response& res, const json& criteria)                   = 0;
    virtual void                    Activate(const crow::request& req, crow::response& res, const json& criteria)                  = 0;
    virtual void                    ResetPassword(const crow::request& req, crow::response& res, const json& reset_json)           = 0;
    virtual void                    GetServices(const crow::request& req, crow::response& res, std::optional<uint64_t> client_id)  = 0;
};
