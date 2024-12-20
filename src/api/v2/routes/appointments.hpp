
#pragma once
#include <drogon/HttpController.h>

#include "api/v2/basic/common.hpp"
#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
#include "entities/appointments/clinic.hpp"
#include "entities/appointments/laboratory.hpp"
#include "entities/appointments/pharmacy.hpp"
#include "entities/appointments/radiologycenter.hpp"

namespace api
{
    namespace v2
    {
        class Appointments : public drogon::HttpController<Appointments>
        {
           public:
            void Create(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Create, req, std::move(callback), req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Read, req, std::move(callback), req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &entityType)
            {
                executeControllerMethod(
                    appointmentRegistry, entityType, &AppointmentControllerBase::Update, req, std::move(callback), req->body(), stoll(req->getParameter("id")));
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &entityType)
            {
                executeControllerMethod(
                    appointmentRegistry, entityType, &AppointmentControllerBase::Delete, req, std::move(callback), stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Search, req, std::move(callback), req->body());
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Appointments::Create, "/{entityType}/create", drogon::Post, SECURE);
            METHOD_ADD(Appointments::Read, "/{entityType}/read", drogon::Post, SECURE);
            METHOD_ADD(Appointments::Update, "/{entityType}/update", drogon::Put, SECURE);
            METHOD_ADD(Appointments::Delete, "{entityType}/delete", drogon::Delete, SECURE);
            METHOD_LIST_END

           private:
            using AppointmentVariant =
                std::variant<std::shared_ptr<AppointmentController<ClinicAppointment>>, std::shared_ptr<AppointmentController<PharmacyAppointment>>,
                    std::shared_ptr<AppointmentController<LaboratoryAppointment>>, std::shared_ptr<AppointmentController<RadiologyCenterAppointment>>>;

            std::unordered_map<std::string_view, AppointmentVariant> appointmentRegistry = {
                {"clinics", Store::getObject<AppointmentController<ClinicAppointment>>()},
                {"pharmacies", Store::getObject<AppointmentController<PharmacyAppointment>>()},
                {"laboratories", Store::getObject<AppointmentController<LaboratoryAppointment>>()},
                {"radiologycenters", Store::getObject<AppointmentController<RadiologyCenterAppointment>>()}};
        };

    }  // namespace v2
}  // namespace api
