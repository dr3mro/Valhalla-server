#pragma once

#include <drogon/drogon.h>
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
    virtual void Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) = 0;
    // virtual void Read(const crow::request& req, crow::response& res, const json& request_json)                                     = 0;
    // virtual void Update(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;
    // virtual void Delete(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params) = 0;
    // virtual void Search(const crow::request& req, crow::response& res, const json& request_json)                                   = 0;

    // virtual std::optional<uint64_t> Login(const crow::request& req, crow::response& res, const json& credentials)                    = 0;
    // virtual void                    Logout(const crow::request& req, crow::response& res, const std::optional<std::string>& token)   = 0;
    // virtual void Suspend(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params)  = 0;
    // virtual void Activate(const crow::request& req, crow::response& res, const std::unordered_map<std::string, std::string>& params) = 0;
    // virtual void ResetPassword(const crow::request& req, crow::response& res, const json& reset_json)                                = 0;
    // virtual void GetServices(const crow::request& req, crow::response& res, std::optional<uint64_t> client_id)                       = 0;
};
