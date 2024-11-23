
#pragma once
#include <drogon/HttpController.h>

#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
#include "controllers/servicecontroller/servicecontroller.hpp"
#include "entities/services/clinics/clinics.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"

namespace api
{
    namespace v2
    {
        class Services : public drogon::HttpController<Services>
        {
           public:
            void Create(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase<CALLBACKSIGNATURE>::Create, std::move(callback),
                                        req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase<CALLBACKSIGNATURE>::Read, std::move(callback),
                                        req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase<CALLBACKSIGNATURE>::Update, std::move(callback),
                                        req->body(), stoll(req->getParameter("id")));
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase<CALLBACKSIGNATURE>::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase<CALLBACKSIGNATURE>::Search, std::move(callback),
                                        req->body());
            }
            METHOD_LIST_BEGIN
            METHOD_ADD(Services::Create, "/{serviceType}/create", drogon::Post, SECURE);
            METHOD_ADD(Services::Read, "/{serviceType}/read", drogon::Post, SECURE);
            METHOD_ADD(Services::Update, "/{serviceType}/update", drogon::Put, SECURE);
            METHOD_ADD(Services::Delete, "/{serviceType}/delete", drogon::Delete, SECURE);
            METHOD_ADD(Services::Search, "/{serviceType}/search", drogon::HttpMethod::Post, SECURE);
            METHOD_LIST_END

           private:
            using ServiceVariant = std::variant<std::shared_ptr<ServiceController<Clinics, CALLBACKSIGNATURE>>,
                                                std::shared_ptr<ServiceController<Pharmacies, CALLBACKSIGNATURE>>,
                                                std::shared_ptr<ServiceController<Laboratories, CALLBACKSIGNATURE>>,
                                                std::shared_ptr<ServiceController<RadiologyCenters, CALLBACKSIGNATURE>>>;

            std::unordered_map<std::string_view, ServiceVariant> serviceRegistry = {
                {"clinics", Store::getObject<ServiceController<Clinics, CALLBACKSIGNATURE>>()},
                {"pharmacies", Store::getObject<ServiceController<Pharmacies, CALLBACKSIGNATURE>>()},
                {"laboratories", Store::getObject<ServiceController<Laboratories, CALLBACKSIGNATURE>>()},
                {"radiologycenters", Store::getObject<ServiceController<RadiologyCenters, CALLBACKSIGNATURE>>()}};
        };
    }  // namespace v2
}  // namespace api