
#pragma once
#include <drogon/HttpController.h>
#include <fmt/format.h>

#include "routes/api/v2/common.hpp"  // IWYU pragma: keep
#include "routes/api/v2/types.hpp"   // IWYU pragma: keep
#include "store/store.hpp"

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

            METHOD_LIST_BEGIN
            METHOD_ADD(Clients::create, "/{clientType}/create", drogon::Post);
            METHOD_LIST_END

           private:
            using ClientVariant = std::variant<std::shared_ptr<ClientController<User>>, std::shared_ptr<ClientController<Provider>>>;

            std::unordered_map<std::string_view, ClientVariant> clientRegistry = {{"users", Store::getObject<ClientController<User>>()},
                                                                                  {"providers", Store::getObject<ClientController<Provider>>()}};
        };

    }  // namespace v2
}  // namespace api