
#pragma once

class StaffControllerBase
{
   public:
    explicit StaffControllerBase() = default;
    virtual ~StaffControllerBase() = default;
    // CRUDS
    virtual void AddStaffToEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)      = 0;
    virtual void RemoveStaffFromEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) = 0;
    virtual void InviteStaffToEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)   = 0;
};
