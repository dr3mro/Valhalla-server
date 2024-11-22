#pragma once

#include <stdint.h>

#include <optional>
#include <string_view>

template <typename CALLBACK>
class ClientControllerBase
{
   public:
    ClientControllerBase() = default;

    virtual ~ClientControllerBase() = default;

    // PUBLIC
    virtual void                    Create(CALLBACK&& callback, std::string_view data)                   = 0;
    virtual void                    Read(CALLBACK&& callback, std::string_view data)                     = 0;
    virtual void                    Update(CALLBACK&& callback, std::string_view data)                   = 0;
    virtual void                    Delete(CALLBACK&& callback, std::optional<uint64_t> client_id)       = 0;
    virtual void                    Search(CALLBACK&& callback, std::string_view data)                   = 0;
    virtual std::optional<uint64_t> Login(CALLBACK&& callback, std::string_view data)                    = 0;
    virtual void                    Logout(CALLBACK&& callback, const std::optional<std::string>& token) = 0;
    virtual void                    Suspend(CALLBACK&& callback, std::optional<uint64_t> client_id)      = 0;
    virtual void                    Activate(CALLBACK&& callback, std::optional<uint64_t> client_id)     = 0;
    virtual void                    ResetPassword(CALLBACK&& callback, std::string_view data)            = 0;
    virtual void                    GetServices(CALLBACK&& callback, std::optional<uint64_t> client_id)  = 0;
};
