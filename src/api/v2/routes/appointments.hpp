
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
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase<CALLBACKSIGNATURE>::Create, std::move(callback),
                                        req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase<CALLBACKSIGNATURE>::Read, std::move(callback),
                                        req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase<CALLBACKSIGNATURE>::Update, std::move(callback),
                                        req->body(), stoll(req->getParameter("id")));
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase<CALLBACKSIGNATURE>::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase<CALLBACKSIGNATURE>::Search, std::move(callback),
                                        req->body());
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Appointments::Create, "/services/{entityType}/appointments/create", drogon::Post, SECURE);
            METHOD_ADD(Appointments::Read, "/services/{entityType}/appointments/read", drogon::Post, SECURE);
            METHOD_ADD(Appointments::Update, "/services/{entityType}/appointments/update", drogon::Put, SECURE);
            METHOD_ADD(Appointments::Delete, "/services/{entityType}/appointments/delete", drogon::Delete, SECURE);
            METHOD_LIST_END

           private:
            using AppointmentVariant = std::variant<std::shared_ptr<AppointmentController<ClinicAppointment, CALLBACKSIGNATURE>>,
                                                    std::shared_ptr<AppointmentController<PharmacyAppointment, CALLBACKSIGNATURE>>,
                                                    std::shared_ptr<AppointmentController<LaboratoryAppointment, CALLBACKSIGNATURE>>,
                                                    std::shared_ptr<AppointmentController<RadiologyCenterAppointment, CALLBACKSIGNATURE>>>;

            std::unordered_map<std::string_view, AppointmentVariant> appointmentRegistry = {
                {"clinics", Store::getObject<AppointmentController<ClinicAppointment, CALLBACKSIGNATURE>>()},
                {"pharmacies", Store::getObject<AppointmentController<PharmacyAppointment, CALLBACKSIGNATURE>>()},
                {"laboratories", Store::getObject<AppointmentController<LaboratoryAppointment, CALLBACKSIGNATURE>>()},
                {"radiologycenters", Store::getObject<AppointmentController<RadiologyCenterAppointment, CALLBACKSIGNATURE>>()}};
        };

    }  // namespace v2
}  // namespace api
