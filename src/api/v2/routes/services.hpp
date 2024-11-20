
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
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase::Create, std::move(callback), req->body());
            }

            void Read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase::Read, std::move(callback), req->body());
            }

            void Update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase::Update, std::move(callback), req->body());
            }
            void Delete(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void Search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &serviceType)
            {
                executeControllerMethod(serviceRegistry, serviceType, &ServiceControllerBase::Search, std::move(callback), req->body());
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Services::Create, "/{serviceType}/create", drogon::Post, ELAPSED, AUTH);
            METHOD_ADD(Services::Read, "/{serviceType}/read", drogon::Post, ELAPSED, AUTH);
            METHOD_ADD(Services::Update, "/{serviceType}/update", drogon::Put, ELAPSED, AUTH);
            METHOD_ADD(Services::Delete, "/{serviceType}/delete", drogon::Delete, ELAPSED, AUTH);
            METHOD_ADD(Services::Search, "/{serviceType}/search", drogon::HttpMethod::Post, ELAPSED, AUTH);
            METHOD_LIST_END

           private:
            using ServiceVariant =
                std::variant<std::shared_ptr<ServiceController<Clinics>>, std::shared_ptr<ServiceController<Pharmacies>>,
                             std::shared_ptr<ServiceController<Laboratories>>, std::shared_ptr<ServiceController<RadiologyCenters>>>;

            std::unordered_map<std::string_view, ServiceVariant> serviceRegistry = {
                {"clinics", Store::getObject<ServiceController<Clinics>>()},
                {"pharmacies", Store::getObject<ServiceController<Pharmacies>>()},
                {"laboratories", Store::getObject<ServiceController<Laboratories>>()},
                {"radiologycenters", Store::getObject<ServiceController<RadiologyCenters>>()}};
        };

    }  // namespace v2
}  // namespace api