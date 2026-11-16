
#pragma once
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "api/v2/basic/common.hpp"
#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "entities/appointments/clinic.hpp"
#include "entities/appointments/laboratory.hpp"
#include "entities/appointments/pharmacy.hpp"
#include "entities/appointments/radiologycenter.hpp"
#include "database/dbexecutor.hpp"
#include "store/store.hpp"
#include "drogon/utils/coroutine.h"

namespace api::v2
{
    class Appointments : public drogon::HttpController<Appointments>
    {
       public:
        void Create(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& entityType)
        {
            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                drogon::async_run([this, req, cb, entityType]() mutable -> drogon::Task<void>
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

                    auto ctlIt = appointmentRegistry.find(entityType);
                    if (ctlIt == appointmentRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", entityType));
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
                });
                
                return;
            }

            executeControllerMethod(*dbExecutor, appointmentRegistry, entityType, &AppointmentControllerBase::Create, req, std::move(callback), req->body());
        }

        void Read(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& entityType)
        {
            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                drogon::async_run([this, req, cb, entityType]() mutable -> drogon::Task<void>
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

                    auto ctlIt = appointmentRegistry.find(entityType);
                    if (ctlIt == appointmentRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", entityType));
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
                });
                
                return;
            }

            executeControllerMethod(*dbExecutor, appointmentRegistry, entityType, &AppointmentControllerBase::Read, req, std::move(callback), req->body());
        }

        void Update(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& entityType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                drogon::async_run([this, req, cb, entityType, id]() mutable -> drogon::Task<void>
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

                    auto ctlIt = appointmentRegistry.find(entityType);
                    if (ctlIt == appointmentRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", entityType));
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
                });
                
                return;
            }

            executeControllerMethod(*dbExecutor, appointmentRegistry, entityType, &AppointmentControllerBase::Update, req, std::move(callback), req->body(), *id);
        }
        void Delete(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& entityType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                drogon::async_run([this, req, cb, entityType, id]() mutable -> drogon::Task<void>
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

                    auto ctlIt = appointmentRegistry.find(entityType);
                    if (ctlIt == appointmentRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", entityType));
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
                });
                
                return;
            }

            executeControllerMethod(*dbExecutor, appointmentRegistry, entityType, &AppointmentControllerBase::Delete, req, std::move(callback), *id);
        }
        void Search(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& entityType)
        {
            executeControllerMethod(*dbExecutor, appointmentRegistry, entityType, &AppointmentControllerBase::Search, req, std::move(callback), req->body());
        }

        METHOD_LIST_BEGIN
        METHOD_ADD(Appointments::Create, "/{entityType}/create", drogon::Post, SECURE);
        METHOD_ADD(Appointments::Read, "/{entityType}/read", drogon::Post, SECURE);
        METHOD_ADD(Appointments::Update, "/{entityType}/update", drogon::Put, SECURE);
        METHOD_ADD(Appointments::Delete, "{entityType}/delete", drogon::Delete, SECURE);
        METHOD_LIST_END

       private:
        std::shared_ptr<DbExecutor> dbExecutor = Store::getObject<DbExecutor>();  // runs the blocking database work, shared with every route
        const bool asyncDb_ = Store::cachedObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
        using AppointmentVariant =
            std::variant<std::shared_ptr<AppointmentController<ClinicAppointment>>, std::shared_ptr<AppointmentController<PharmacyAppointment>>,
                std::shared_ptr<AppointmentController<LaboratoryAppointment>>, std::shared_ptr<AppointmentController<RadiologyCenterAppointment>>>;

        std::unordered_map<std::string_view, AppointmentVariant> appointmentRegistry = {
            {"clinics", Store::getObject<AppointmentController<ClinicAppointment>>()},
            {"pharmacies", Store::getObject<AppointmentController<PharmacyAppointment>>()},
            {"laboratories", Store::getObject<AppointmentController<LaboratoryAppointment>>()},
            {"radiologycenters", Store::getObject<AppointmentController<RadiologyCenterAppointment>>()}};
    };

}  // namespace api::v2
