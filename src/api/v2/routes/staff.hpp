
#pragma once
#include <drogon/HttpController.h>

#include <string>

#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
#include "controllers/staffcontroller/staffcontroller.hpp"
#include "entities/services/clinics/clinics.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"

namespace api
{
    namespace v2
    {
        class Staff : public drogon::HttpController<Staff>
        {
           public:
            void Invite(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
            {
                executeControllerMethod(staffRegistry, serviceType, &StaffControllerBase::InviteStaffToEntity, req, std::move(callback), req->body());
            }

            void Add(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
            {
                executeControllerMethod(staffRegistry, serviceType, &StaffControllerBase::AddStaffToEntity, req, std::move(callback), req->body());
            }

            void Remove(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
            {
                executeControllerMethod(staffRegistry, serviceType, &StaffControllerBase::RemoveStaffFromEntity, req, std::move(callback), req->body());
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Staff::Invite, "/services/{serviceType}/staff/invite", drogon::Post, SECURE);
            METHOD_ADD(Staff::Add, "/services/{serviceType}/staff/add", drogon::Post, SECURE);
            METHOD_ADD(Staff::Remove, "/services/{serviceType}/staff/remove", drogon::Post, SECURE);
            METHOD_LIST_END

           private:
            using StaffVariant = std::variant<std::shared_ptr<StaffController<Clinics>>, std::shared_ptr<StaffController<Pharmacies>>,
                std::shared_ptr<StaffController<Laboratories>>, std::shared_ptr<StaffController<RadiologyCenters>>>;

            std::unordered_map<std::string_view, StaffVariant> staffRegistry = {{"clinics", Store::getObject<StaffController<Clinics>>()},
                {"pharmacies", Store::getObject<StaffController<Pharmacies>>()}, {"laboratories", Store::getObject<StaffController<Laboratories>>()},
                {"radiologycenters", Store::getObject<StaffController<RadiologyCenters>>()}};
        };

    }  // namespace v2
}  // namespace api
