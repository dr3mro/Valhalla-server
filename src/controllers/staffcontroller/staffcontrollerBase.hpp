
#pragma once

#include <functional>
#include <string_view>

#include "utils/global/callback.hpp"
class StaffControllerBase
{
   public:
    explicit StaffControllerBase() = default;
    virtual ~StaffControllerBase() = default;
    // CRUDS
    virtual void AddStaffToEntity(CALLBACK_ &&callback, std::string_view data)      = 0;
    virtual void RemoveStaffFromEntity(CALLBACK_ &&callback, std::string_view data) = 0;
    virtual void InviteStaffToEntity(CALLBACK_ &&callback, std::string_view data)   = 0;
};
