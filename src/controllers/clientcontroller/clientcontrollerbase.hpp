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
    virtual void                    CreateClient(const crow::request& req, crow::response& res, const json& criteria)                    = 0;
    virtual std::optional<uint64_t> AuthenticateClient(const crow::request& req, crow::response& res, const jsoncons::json& credentials) = 0;
    virtual void                    ReadClient(const crow::request& req, crow::response& res, const json& criteria)                      = 0;
    virtual void                    UpdateClient(const crow::request& req, crow::response& res, const json& criteria)                    = 0;
    virtual void                    DeleteClient(const crow::request& req, crow::response& res, const json& delete_json)                 = 0;
    virtual void                    SearchClient(const crow::request& req, crow::response& res, const json& search_json)                 = 0;
    virtual void                    LogoutClient(const crow::request& req, crow::response& res, const std::optional<std::string>& token) = 0;
    virtual void                    SuspendClient(const crow::request& req, crow::response& res, const json& criteria)                   = 0;
    virtual void                    ActivateClient(const crow::request& req, crow::response& res, const json& criteria)                  = 0;
    virtual void                    ResetPassword(const crow::request& req, crow::response& res, const json& reset_json)                 = 0;
    virtual void                    GetServices(const crow::request& req, crow::response& res, std::optional<uint64_t> client_id)        = 0;
};
