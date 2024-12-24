#pragma once

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

#include "utils/global/callback.hpp"
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;
class ClientControllerBase
{
   public:
    ClientControllerBase() = default;

    ClientControllerBase(const ClientControllerBase&)            = default;
    ClientControllerBase(ClientControllerBase&&)                 = default;
    ClientControllerBase& operator=(const ClientControllerBase&) = default;
    ClientControllerBase& operator=(ClientControllerBase&&)      = default;
    virtual ~ClientControllerBase()                              = default;

    // PUBLIC
    virtual void Create(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                                                  = 0;
    virtual void Read(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                                                    = 0;
    virtual void Update(CALLBACK_&& callback, const Requester&& requester, std::string_view data, std::optional<uint64_t> _id)                     = 0;
    virtual void Delete(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id)                                      = 0;
    virtual void Search(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                                                  = 0;
    virtual void Login(CALLBACK_&& callback, const Requester&& requester, std::string_view data, const std::string& ip_address)                    = 0;
    virtual void Logout(CALLBACK_&& callback, const Requester&& requester, const std::optional<std::string>& token, const std::string& ip_address) = 0;
    virtual void Suspend(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id)                                     = 0;
    virtual void Activate(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id)                                    = 0;
    virtual void ResetPassword(CALLBACK_&& callback, const Requester&& requester, std::string_view data)                                           = 0;
    virtual void GetServices(CALLBACK_&& callback, const Requester&& requester, std::optional<uint64_t> client_id)                                 = 0;
};
