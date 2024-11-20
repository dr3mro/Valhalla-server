
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
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Create, std::move(callback), req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Read, std::move(callback), req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Update, std::move(callback), req->body());
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
            {
                executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::Search, std::move(callback), req->body());
            }

            void GetVisits(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
            {
                executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::GetVisits, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Clinic::Create, "/{serviceType}/create", drogon::Post, ELAPSED, AUTH);
            METHOD_ADD(Clinic::Read, "/{serviceType}/read", drogon::Post, ELAPSED, AUTH);
            METHOD_ADD(Clinic::Update, "/{serviceType}/update", drogon::Put, ELAPSED, AUTH);
            METHOD_ADD(Clinic::Delete, "/{serviceType}/delete", drogon::Delete, ELAPSED, AUTH);
            METHOD_ADD(Clinic::Search, "/patients/search", drogon::HttpMethod::Post, ELAPSED, AUTH);
            METHOD_ADD(Clinic::GetVisits, "/patients/getvisits", drogon::Get, ELAPSED, AUTH);
            METHOD_LIST_END

           private:
            using ClinicVariant = std::variant<std::shared_ptr<ClinicController<Patient>>, std::shared_ptr<ClinicController<Visits>>,
                                               std::shared_ptr<ClinicController<VisitDrugs>>, std::shared_ptr<ClinicController<Requests>>,
                                               std::shared_ptr<ClinicController<Prescriptions>>, std::shared_ptr<ClinicController<PaidServices>>,
                                               std::shared_ptr<ClinicController<Reports>>, std::shared_ptr<ClinicController<PatientDrugs>>,
                                               std::shared_ptr<ClinicController<Health>>>;

            std::unordered_map<std::string_view, ClinicVariant> clinicRegistry = {
                {"patients", Store::getObject<ClinicController<Patient>>()},
                {"visits", Store::getObject<ClinicController<Visits>>()},
                {"visitDrugs", Store::getObject<ClinicController<VisitDrugs>>()},
                {"requests", Store::getObject<ClinicController<Requests>>()},
                {"prescriptions", Store::getObject<ClinicController<Prescriptions>>()},
                {"paidservices", Store::getObject<ClinicController<PaidServices>>()},
                {"reports", Store::getObject<ClinicController<Reports>>()},
                {"patientdrugs", Store::getObject<ClinicController<PatientDrugs>>()},
                {"health", Store::getObject<ClinicController<Health>>()}};
        };

    }  // namespace v2
}  // namespace api
