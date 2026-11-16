
#pragma once

#include <string_view>

#include "drogon/utils/coroutine.h"
#include "utils/global/callback.hpp"
#include "utils/global/requester.hpp"
using Requester = api::v2::Requester;
class StaffControllerBase
{
   public:
    explicit StaffControllerBase()                             = default;
    StaffControllerBase(const StaffControllerBase&)            = default;
    StaffControllerBase(StaffControllerBase&&)                 = delete;
    StaffControllerBase& operator=(const StaffControllerBase&) = default;
    StaffControllerBase& operator=(StaffControllerBase&&)      = delete;
    virtual ~StaffControllerBase()                             = default;
    // CRUDS
    virtual void AddStaffToEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data)      = 0;
    virtual void RemoveStaffFromEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data) = 0;
    virtual void InviteStaffToEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data)   = 0;
    virtual drogon::Task<void> AddStaffToEntityAsync(CALLBACK_&& callback, const Requester&& requester, std::string_view data)    = 0;
    virtual drogon::Task<void> RemoveStaffFromEntityAsync(CALLBACK_&& callback, const Requester&& requester, std::string_view data) = 0;
    virtual drogon::Task<void> InviteStaffToEntityAsync(CALLBACK_&& callback, const Requester&& requester, std::string_view data)  = 0;
};
