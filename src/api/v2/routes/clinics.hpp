
#pragma once
#include <drogon/drogon.h>

#include "api/v2/basic/common.hpp"
#include "controllers/cliniccontroller/cliniccontroller.hpp"
#include "entities/services/clinics/patient/health.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/patient/patientdrugs.hpp"
#include "entities/services/clinics/patient/reports.hpp"
#include "entities/services/clinics/visits/paidservices.hpp"
#include "entities/services/clinics/visits/prescriptions.hpp"
#include "entities/services/clinics/visits/requests.hpp"
#include "entities/services/clinics/visits/visitdrugs.hpp"

namespace api
{
    namespace v2
    {
        class Clinic : public drogon::HttpController<Clinic>
        {
           public:
            void Create(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase<CALLBACKSIGNATURE>::Create, std::move(callback),
                                        req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase<CALLBACKSIGNATURE>::Read, std::move(callback),
                                        req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase<CALLBACKSIGNATURE>::Update, std::move(callback),
                                        req->body());
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase<CALLBACKSIGNATURE>::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
            {
                executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase<CALLBACKSIGNATURE>::Search, std::move(callback),
                                        req->body());
            }

            void GetVisits(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
            {
                executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase<CALLBACKSIGNATURE>::GetVisits, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Clinic::Create, "/{serviceType}/create", drogon::Post, SECURE);
            METHOD_ADD(Clinic::Read, "/{serviceType}/read", drogon::Post, SECURE);
            METHOD_ADD(Clinic::Update, "/{serviceType}/update", drogon::Put, SECURE);
            METHOD_ADD(Clinic::Delete, "/{serviceType}/delete", drogon::Delete, SECURE);
            METHOD_ADD(Clinic::Search, "/patients/search", drogon::HttpMethod::Post, SECURE);
            METHOD_ADD(Clinic::GetVisits, "/patients/getvisits", drogon::Get, SECURE);
            METHOD_LIST_END

           private:
            using ClinicVariant = std::variant<
                std::shared_ptr<ClinicController<Patient, CALLBACKSIGNATURE>>, std::shared_ptr<ClinicController<Visits, CALLBACKSIGNATURE>>,
                std::shared_ptr<ClinicController<VisitDrugs, CALLBACKSIGNATURE>>, std::shared_ptr<ClinicController<Requests, CALLBACKSIGNATURE>>,
                std::shared_ptr<ClinicController<Prescriptions, CALLBACKSIGNATURE>>,
                std::shared_ptr<ClinicController<PaidServices, CALLBACKSIGNATURE>>, std::shared_ptr<ClinicController<Reports, CALLBACKSIGNATURE>>,
                std::shared_ptr<ClinicController<PatientDrugs, CALLBACKSIGNATURE>>, std::shared_ptr<ClinicController<Health, CALLBACKSIGNATURE>>>;

            std::unordered_map<std::string_view, ClinicVariant> clinicRegistry = {
                {"patients", Store::getObject<ClinicController<Patient, CALLBACKSIGNATURE>>()},
                {"visits", Store::getObject<ClinicController<Visits, CALLBACKSIGNATURE>>()},
                {"visitDrugs", Store::getObject<ClinicController<VisitDrugs, CALLBACKSIGNATURE>>()},
                {"requests", Store::getObject<ClinicController<Requests, CALLBACKSIGNATURE>>()},
                {"prescriptions", Store::getObject<ClinicController<Prescriptions, CALLBACKSIGNATURE>>()},
                {"paidservices", Store::getObject<ClinicController<PaidServices, CALLBACKSIGNATURE>>()},
                {"reports", Store::getObject<ClinicController<Reports, CALLBACKSIGNATURE>>()},
                {"patientdrugs", Store::getObject<ClinicController<PatientDrugs, CALLBACKSIGNATURE>>()},
                {"health", Store::getObject<ClinicController<Health, CALLBACKSIGNATURE>>()}};
        };
    }  // namespace v2
}  // namespace api
