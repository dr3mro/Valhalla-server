
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
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Create, std::move(callback),
                                        req->body());
            }

            void login(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                       const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Login, std::move(callback),
                                        req->body());
            }
            void logout(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Logout, std::move(callback),
                                        req->getHeader("Authorization").substr(7));
            }

            void suspend(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                         const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Suspend, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            void activate(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                          const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Activate, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            void read(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                      const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Read, std::move(callback), req->body());
            }

            void update(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Update, std::move(callback),
                                        req->body());
            }
            void delete_(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                         const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Delete, std::move(callback),
                                        stoll(req->getParameter("id")));
            }
            void search(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::Search, std::move(callback),
                                        req->body());
            }
            void getservices(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                             const std::string &clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase<CALLBACKSIGNATURE>::GetServices, std::move(callback),
                                        stoll(req->getParameter("id")));
            }

            METHOD_LIST_BEGIN
            METHOD_ADD(Clients::create, "/{clientType}/create", drogon::Post, INSECURE);
            METHOD_ADD(Clients::login, "/{clientType}/login", drogon::Post, INSECURE);
            METHOD_ADD(Clients::logout, "/{clientType}/logout", drogon::Post, SECURE);
            METHOD_ADD(Clients::suspend, "/{clientType}/suspend", drogon::Post, SECURE);
            METHOD_ADD(Clients::activate, "/{clientType}/activate", drogon::Post, SECURE);
            METHOD_ADD(Clients::read, "/{clientType}/read", drogon::Post, SECURE);
            METHOD_ADD(Clients::update, "/{clientType}/update", drogon::Put, SECURE);
            METHOD_ADD(Clients::delete_, "/{clientType}/delete", drogon::Delete, SECURE);
            METHOD_ADD(Clients::search, "/{clientType}/search", drogon::Post, SECURE);
            METHOD_ADD(Clients::getservices, "/{clientType}/services", drogon::Get, SECURE);
            METHOD_LIST_END

           private:
            using ClientVariant = std::variant<std::shared_ptr<ClientController<User, CALLBACKSIGNATURE>>,
                                               std::shared_ptr<ClientController<Provider, CALLBACKSIGNATURE>>>;

            std::unordered_map<std::string_view, ClientVariant> clientRegistry = {
                {"users", Store::getObject<ClientController<User, CALLBACKSIGNATURE>>()},
                {"providers", Store::getObject<ClientController<Provider, CALLBACKSIGNATURE>>()}};
        };

    }  // namespace v2
}  // namespace api