
#pragma once
#include <drogon/HttpController.h>

#include "api/v2/basic/common.hpp"  // IWYU pragma: keep
#include "controllers/clientcontroller/clientcontroller.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"

namespace api
{
    namespace v2
    {
        class Clients : public drogon::HttpController<Clients>
        {
           public:
            void create(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Create, std::move(callback), req->body());
            }

            void login(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                       const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Login, std::move(callback), req->body());
            }
            void logout(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Logout, std::move(callback),
                                        req->getHeader("Authorization").substr(7));
            }

            void suspend(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                         const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Suspend, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            void activate(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                          const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Activate, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            void read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Read, std::move(callback), req->body());
            }

            void update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Update, std::move(callback), req->body());
            }
            void delete_(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                         const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Search, std::move(callback), req->body());
            }
            void getservices(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                             const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::GetServices, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Clients::create, "/{clientType}/create", drogon::Post);
            METHOD_ADD(Clients::login, "/{clientType}/login", drogon::Post);
            METHOD_ADD(Clients::logout, "/{clientType}/logout", drogon::Post);
            METHOD_ADD(Clients::suspend, "/{clientType}/suspend", drogon::Post);
            METHOD_ADD(Clients::activate, "/{clientType}/activate", drogon::Post);
            METHOD_ADD(Clients::read, "/{clientType}/read", drogon::Post);
            METHOD_ADD(Clients::update, "/{clientType}/update", drogon::Put);
            METHOD_ADD(Clients::delete_, "/{clientType}/delete", drogon::Delete);
            METHOD_ADD(Clients::search, "/{clientType}/search", drogon::HttpMethod::Post);
            METHOD_ADD(Clients::getservices, "/{clientType}/services", drogon::Get);
            METHOD_LIST_END

           private:
            using ClientVariant = std::variant<std::shared_ptr<ClientController<User>>, std::shared_ptr<ClientController<Provider>>>;

            std::unordered_map<std::string_view, ClientVariant> clientRegistry = {{"users", Store::getObject<ClientController<User>>()},
                                                                                  {"providers", Store::getObject<ClientController<Provider>>()}};
        };

    }  // namespace v2
}  // namespace api