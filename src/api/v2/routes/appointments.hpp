
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
            void Create(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Create, std::move(callback), req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Read, std::move(callback), req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Update, std::move(callback), req->body());
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Search, std::move(callback), req->body());
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Appointments::Create, "/services/{entityType}/appointments/create", drogon::Post);
            METHOD_ADD(Appointments::Read, "/services/{entityType}/appointments/read", drogon::Post);
            METHOD_ADD(Appointments::Update, "/services/{entityType}/appointments/update", drogon::Put);
            METHOD_ADD(Appointments::Delete, "/services/{entityType}/appointments/delete", drogon::Delete);
            METHOD_LIST_END

           private:
            using AppointmentVariant =
                std::variant<std::shared_ptr<AppointmentController<ClinicAppointment>>, std::shared_ptr<AppointmentController<PharmacyAppointment>>,
                             std::shared_ptr<AppointmentController<LaboratoryAppointment>>,
                             std::shared_ptr<AppointmentController<RadiologyCenterAppointment>>>;

            std::unordered_map<std::string_view, AppointmentVariant> appointmentRegistry = {
                {"clinics", Store::getObject<AppointmentController<ClinicAppointment>>()},
                {"pharmacies", Store::getObject<AppointmentController<PharmacyAppointment>>()},
                {"laboratories", Store::getObject<AppointmentController<LaboratoryAppointment>>()},
                {"radiologycenters", Store::getObject<AppointmentController<RadiologyCenterAppointment>>()}};
        };

    }  // namespace v2
}  // namespace api