
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

#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
#include "controllers/servicecontroller/servicecontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
#include "entities/services/clinics/clinics.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"
#include "database/dbexecutor.hpp"
#include "store/store.hpp"
#include "drogon/utils/coroutine.h"

namespace api::v2
{
    class Services : public drogon::HttpController<Services>
    {
       public:
        void Create(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            executeControllerMethod(*dbExecutor, serviceRegistry, serviceType, &ServiceControllerBase::Create, req, std::move(callback), req->body());
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

                    auto ctlIt = serviceRegistry.find(serviceType);
                    if (ctlIt == serviceRegistry.end())
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

            executeControllerMethod(*dbExecutor, serviceRegistry, serviceType, &ServiceControllerBase::Read, req, std::move(callback), req->body());
        }

        void Update(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;
            executeControllerMethod(*dbExecutor, serviceRegistry, serviceType, &ServiceControllerBase::Update, req, std::move(callback), req->body(), *id);
        }
        void Delete(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;
            executeControllerMethod(*dbExecutor, serviceRegistry, serviceType, &ServiceControllerBase::Delete, req, std::move(callback), *id);
        }
        void Search(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& serviceType)
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

                    auto ctlIt = serviceRegistry.find(serviceType);
                    if (ctlIt == serviceRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", serviceType));
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

            executeControllerMethod(*dbExecutor, serviceRegistry, serviceType, &ServiceControllerBase::Search, req, std::move(callback), req->body());
        }
        METHOD_LIST_BEGIN
        METHOD_ADD(Services::Create, "/{serviceType}/create", drogon::Post, SECURE);
        METHOD_ADD(Services::Read, "/{serviceType}/read", drogon::Post, SECURE);
        METHOD_ADD(Services::Update, "/{serviceType}/update", drogon::Put, SECURE);
        METHOD_ADD(Services::Delete, "/{serviceType}/delete", drogon::Delete, SECURE);
        METHOD_ADD(Services::Search, "/{serviceType}/search", drogon::HttpMethod::Post, SECURE);
        METHOD_LIST_END

       private:
        std::shared_ptr<DbExecutor> dbExecutor = Store::getObject<DbExecutor>();  // runs the blocking database work, shared with every route
        using ServiceVariant = std::variant<std::shared_ptr<ServiceController<Clinics>>, std::shared_ptr<ServiceController<Pharmacies>>,
            std::shared_ptr<ServiceController<Laboratories>>, std::shared_ptr<ServiceController<RadiologyCenters>>>;

        std::unordered_map<std::string_view, ServiceVariant> serviceRegistry = {{"clinics", Store::getObject<ServiceController<Clinics>>()},
            {"pharmacies", Store::getObject<ServiceController<Pharmacies>>()}, {"laboratories", Store::getObject<ServiceController<Laboratories>>()},
            {"radiologycenters", Store::getObject<ServiceController<RadiologyCenters>>()}};
    };
}  // namespace api::v2
