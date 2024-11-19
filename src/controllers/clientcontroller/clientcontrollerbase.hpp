#pragma once

#include <drogon/drogon.h>
#include <stdint.h>

#include <functional>
#include <optional>
#include <string_view>

class ClientControllerBase
{
   public:
    ClientControllerBase() = default;

    virtual ~ClientControllerBase() = default;

    // PUBLIC
    virtual void                    Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)             = 0;
    virtual void                    Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)               = 0;
    virtual void                    Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)             = 0;
    virtual void                    Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> client_id) = 0;
    virtual void                    Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)             = 0;
    virtual std::optional<uint64_t> Login(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)              = 0;
    virtual void Logout(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::optional<std::string>& token)              = 0;
    virtual void Suspend(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> client_id)                   = 0;
    virtual void Activate(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> client_id)                  = 0;
    virtual void ResetPassword(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)                         = 0;
    virtual void GetServices(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> client_id)               = 0;
};
