
#pragma once
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "api/v2/basic/common.hpp"
#include "controllers/cliniccontroller/cliniccontroller.hpp"
#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
#include "entities/services/clinics/patient/health.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/patient/patientdrugs.hpp"
#include "entities/services/clinics/patient/reports.hpp"
#include "entities/services/clinics/visits/paidservices.hpp"
#include "entities/services/clinics/visits/prescriptions.hpp"
#include "entities/services/clinics/visits/requests.hpp"
#include "entities/services/clinics/visits/visitdrugs.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "store/store.hpp"

namespace api::v2
{
    class Clinic : public drogon::HttpController<Clinic>
    {
       public:
        void Create(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
        {
            executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Create, req, std::move(callback), req->body());
        }

        void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
        {
            executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Read, req, std::move(callback), req->body());
        }

        void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
        {
            executeControllerMethod(
                clinicRegistry, serviceType, &ClinicControllerBase::Update, req, std::move(callback), req->body(), stoll(req->getParameter("id")));
        }
        void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::string &serviceType)
        {
            executeControllerMethod(clinicRegistry, serviceType, &ClinicControllerBase::Delete, req, std::move(callback), stoll(req->getParameter("id")));
        }
        void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::Search, req, std::move(callback), req->body());
        }

        void GetVisits(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            executeControllerMethod(
                clinicRegistry, "patients", &ClinicControllerBase::GetVisits, req, std::move(callback), stoll(req->getParameter("patient_id")));
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
        using ClinicVariant =
            std::variant<std::shared_ptr<ClinicController<Patient>>, std::shared_ptr<ClinicController<Visits>>, std::shared_ptr<ClinicController<VisitDrugs>>,
                std::shared_ptr<ClinicController<Requests>>, std::shared_ptr<ClinicController<Prescriptions>>, std::shared_ptr<ClinicController<PaidServices>>,
                std::shared_ptr<ClinicController<Reports>>, std::shared_ptr<ClinicController<PatientDrugs>>, std::shared_ptr<ClinicController<Health>>>;

        std::unordered_map<std::string_view, ClinicVariant> clinicRegistry = {{"patients", Store::getObject<ClinicController<Patient>>()},
            {"visits", Store::getObject<ClinicController<Visits>>()}, {"visitDrugs", Store::getObject<ClinicController<VisitDrugs>>()},
            {"requests", Store::getObject<ClinicController<Requests>>()}, {"prescriptions", Store::getObject<ClinicController<Prescriptions>>()},
            {"paidservices", Store::getObject<ClinicController<PaidServices>>()}, {"reports", Store::getObject<ClinicController<Reports>>()},
            {"patientdrugs", Store::getObject<ClinicController<PatientDrugs>>()}, {"health", Store::getObject<ClinicController<Health>>()}};
    };
}  // namespace api::v2
