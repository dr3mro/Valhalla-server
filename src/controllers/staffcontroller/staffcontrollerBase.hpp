
#pragma once

#include <string_view>
template <typename CALLBACK>
class StaffControllerBase
{
   public:
    explicit StaffControllerBase() = default;
    virtual ~StaffControllerBase() = default;
    // CRUDS
    virtual void AddStaffToEntity(CALLBACK &&callback, std::string_view data)      = 0;
    virtual void RemoveStaffFromEntity(CALLBACK &&callback, std::string_view data) = 0;
    virtual void InviteStaffToEntity(CALLBACK &&callback, std::string_view data)   = 0;
};
