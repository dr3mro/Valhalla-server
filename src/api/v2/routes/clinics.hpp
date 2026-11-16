
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
#include "database/dbexecutor.hpp"
#include "store/store.hpp"
#include "drogon/utils/coroutine.h"

namespace api::v2
{
    class Clinic : public drogon::HttpController<Clinic>
    {
       public:
        void Create(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            const bool async = Store::getObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, serviceType]() mutable -> drogon::AsyncTask
                {
                    CALLBACK_ mcb = [cb](int code, const std::string& content)
                    {
                        using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
                        switch (code)
                        {
                            case api::v2::Http::Status::OK:
                                Helper::successResponse(content, Reply(cb));
                                break;
                            case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                Helper::failureResponse(content, Reply(cb));
                                break;
                            default:
                                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, Reply(cb));
                                break;
                        }
                    };

                    Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                    auto ctlIt = clinicRegistry.find(serviceType);
                    if (ctlIt == clinicRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", serviceType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->CreateAsync(std::move(mcb), std::move(requester), req->body());
                        }, ctlIt->second);
                    }
                    catch (const std::exception& e)
                    {
                        Helper::failureResponse(e.what(), std::move(cb));
                    }
                };
                handler();
                return;
            }

            executeControllerMethod(*dbExecutor, clinicRegistry, serviceType, &ClinicControllerBase::Create, req, std::move(callback), req->body());
        }

        void Read(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            const bool async = Store::getObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, serviceType]() mutable -> drogon::AsyncTask
                {
                    CALLBACK_ mcb = [cb](int code, const std::string& content)
                    {
                        using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
                        switch (code)
                        {
                            case api::v2::Http::Status::OK:
                                Helper::successResponse(content, Reply(cb));
                                break;
                            case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                Helper::failureResponse(content, Reply(cb));
                                break;
                            default:
                                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, Reply(cb));
                                break;
                        }
                    };

                    Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                    auto ctlIt = clinicRegistry.find(serviceType);
                    if (ctlIt == clinicRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", serviceType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->ReadAsync(std::move(mcb), std::move(requester), req->body());
                        }, ctlIt->second);
                    }
                    catch (const std::exception& e)
                    {
                        Helper::failureResponse(e.what(), std::move(cb));
                    }
                };
                handler();
                return;
            }

            executeControllerMethod(*dbExecutor, clinicRegistry, serviceType, &ClinicControllerBase::Read, req, std::move(callback), req->body());
        }

        void Update(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = Store::getObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, serviceType, id]() mutable -> drogon::AsyncTask
                {
                    CALLBACK_ mcb = [cb](int code, const std::string& content)
                    {
                        using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
                        switch (code)
                        {
                            case api::v2::Http::Status::OK:
                                Helper::successResponse(content, Reply(cb));
                                break;
                            case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                Helper::failureResponse(content, Reply(cb));
                                break;
                            default:
                                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, Reply(cb));
                                break;
                        }
                    };

                    Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                    auto ctlIt = clinicRegistry.find(serviceType);
                    if (ctlIt == clinicRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", serviceType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->UpdateAsync(std::move(mcb), std::move(requester), req->body(), id);
                        }, ctlIt->second);
                    }
                    catch (const std::exception& e)
                    {
                        Helper::failureResponse(e.what(), std::move(cb));
                    }
                };
                handler();
                return;
            }

            executeControllerMethod(*dbExecutor, clinicRegistry, serviceType, &ClinicControllerBase::Update, req, std::move(callback), req->body(), *id);
        }
        void Delete(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = Store::getObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, serviceType, id]() mutable -> drogon::AsyncTask
                {
                    CALLBACK_ mcb = [cb](int code, const std::string& content)
                    {
                        using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
                        switch (code)
                        {
                            case api::v2::Http::Status::OK:
                                Helper::successResponse(content, Reply(cb));
                                break;
                            case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                Helper::failureResponse(content, Reply(cb));
                                break;
                            default:
                                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, Reply(cb));
                                break;
                        }
                    };

                    Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                    auto ctlIt = clinicRegistry.find(serviceType);
                    if (ctlIt == clinicRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", serviceType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->DeleteAsync(std::move(mcb), std::move(requester), id);
                        }, ctlIt->second);
                    }
                    catch (const std::exception& e)
                    {
                        Helper::failureResponse(e.what(), std::move(cb));
                    }
                };
                handler();
                return;
            }

            executeControllerMethod(*dbExecutor, clinicRegistry, serviceType, &ClinicControllerBase::Delete, req, std::move(callback), *id);
        }
        void Search(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
        {
            const bool async = Store::getObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb]() mutable -> drogon::AsyncTask
                {
                    CALLBACK_ mcb = [cb](int code, const std::string& content)
                    {
                        using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
                        switch (code)
                        {
                            case api::v2::Http::Status::OK:
                                Helper::successResponse(content, Reply(cb));
                                break;
                            case api::v2::Http::Status::INTERNAL_SERVER_ERROR:
                                Helper::failureResponse(content, Reply(cb));
                                break;
                            default:
                                Helper::errorResponse(static_cast<drogon::HttpStatusCode>(code), content, Reply(cb));
                                break;
                        }
                    };

                    Requester requester(req->getAttributes()->get<uint64_t>("clientID"), req->getAttributes()->get<std::string>("clientGroup"));

                    auto ctlIt = clinicRegistry.find("patients");
                    if (ctlIt == clinicRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, "Type mapping not found for: patients");
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->SearchAsync(std::move(mcb), std::move(requester), req->body());
                        }, ctlIt->second);
                    }
                    catch (const std::exception& e)
                    {
                        Helper::failureResponse(e.what(), std::move(cb));
                    }
                };
                handler();
                return;
            }

            executeControllerMethod(*dbExecutor, clinicRegistry, "patients", &ClinicControllerBase::Search, req, std::move(callback), req->body());
        }

        void GetVisits(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
        {
            auto patient_id = parseIdParam(req, "patient_id", callback);
            if (!patient_id)
                return;
            executeControllerMethod(*dbExecutor, clinicRegistry, "patients", &ClinicControllerBase::GetVisits, req, std::move(callback), *patient_id);
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
        std::shared_ptr<DbExecutor> dbExecutor = Store::getObject<DbExecutor>();  // runs the blocking database work, shared with every route
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
