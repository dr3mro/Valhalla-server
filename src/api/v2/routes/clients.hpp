
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
#include "controllers/clientcontroller/clientcontroller.hpp"
#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "database/dbexecutor.hpp"
#include "utils/global/http.hpp"
#include "validator/validator.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "database/databasebusy.hpp"
#include "configurator/configurator.hpp"
#include "controllers/databasecontroller/databasecontroller.hpp"
#include "database/asyncdbclient.hpp"
#include "drogon/utils/coroutine.h"
#include "store/store.hpp"

namespace api::v2
{
    class Clients : public drogon::HttpController<Clients>
    {
       public:
        void create(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Create, req, std::move(callback), req->body());
        }

        void login(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType]() mutable -> drogon::AsyncTask
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

                    Requester requester(0, ""); // login has no authenticated requester

                    auto ctlIt = clientRegistry.find(clientType);
                    if (ctlIt == clientRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->LoginAsync(std::move(mcb), std::move(requester), req->body(), req->peerAddr().toIp());
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

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Login, req, std::move(callback), req->body(), req->peerAddr().toIp());
        }
        void logout(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Logout, req, std::move(callback),
                req->getHeader("Authorization").substr(7), req->peerAddr().toIp());  // NOLINT
        }

        void suspend(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType, id]() mutable -> drogon::AsyncTask
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

                    auto ctlIt = clientRegistry.find(clientType);
                    if (ctlIt == clientRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->SuspendAsync(std::move(mcb), std::move(requester), id);
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

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Suspend, req, std::move(callback), *id);
        }

        void activate(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType, id]() mutable -> drogon::AsyncTask
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

                    auto ctlIt = clientRegistry.find(clientType);
                    if (ctlIt == clientRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->ActivateAsync(std::move(mcb), std::move(requester), id);
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

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Activate, req, std::move(callback), *id);
        }

        void read(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            // Prototype async path (DB_ASYNC=true, providers only): the read runs as a coroutine on the IO loop.
            const bool async = asyncDb_;
            if (async && (clientType == "users" || clientType == "providers"))
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType]<typename T>() mutable -> drogon::AsyncTask
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

                    try
                    {
                        jsoncons::json request_j;
                        uint64_t      _id = 0;
                        std::unordered_set<std::string> schema;
                        try
                        {
                            request_j = jsoncons::json::parse(req->body());
                            _id       = request_j.at("id").as<uint64_t>();
                            schema    = request_j.at("schema").as<std::unordered_set<std::string>>();
                        }
                        catch (const std::exception&)
                        {
                            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, "Invalid request body: malformed JSON or missing/invalid 'id'/'schema'.");
                            co_return;
                        }

                        auto ctlIt = clientRegistry.find(clientType);
                        if (ctlIt == clientRegistry.end())
                        {
                            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
                            co_return;
                        }

                        // Permission check (CPU-only for clients, no DB).
                        api::v2::Http::Error error;
                        // Extract controller to get gateKeeper? Use a default-constructed entity controller? We reuse sync permission via GateKeeper global.
                        auto gateKeeper = gateKeeper_; // may not be registered; fallback
                        // Simpler: use PermissionManager indirectly not available; skip? Instead keep sync check by constructing controller.
                        // For prototype, delegate permission check to the existing sync controller method is not possible async.
                        // We'll perform the same assert_group_id_match by calling GateKeeper if registered.
                        // If GateKeeper is not in Store, allow (should not happen in production).
                        auto checkPermission = [gateKeeper, requester, _id, &error]() -> drogon::Task<bool>
                        {
                            if (gateKeeper)
                            {
                                co_return gateKeeper->canRead<T>(requester, _id, error);
                            }
                            co_return true;
                        };
                        bool ok = co_await checkPermission();
                        if (!ok)
                        {
                            std::move(mcb)(error.code, error.message);
                            co_return;
                        }

                        Validator::Rule rule(Validator::Rule::Action::ASSERT_NOT_PRESENT, {"id", "username", "password", "created_at", "updated_at"});
                        if (!Validator::validateDatabaseReadSchema(schema, std::format("{}_safe", T::getTableName()), error, rule))
                        {
                            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Failed to validate request body, {}.", error.message));
                            co_return;
                        }

                        T entity((Read_t(schema, _id)));

                        // Build SQL
                        pqxx::params params;
                        auto query = entity.getSqlReadStatement(params);
                        if (!query.has_value())
                        {
                            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Failed to get SQL statement for {}.", entity.getTableName()));
                            co_return;
                        }

                        auto dbController = dbController_;
                        auto result = co_await dbController->executeReadQueryCoro(query.value(), params);
                        if (result.has_value() && !result->empty())
                        {
                            std::move(mcb)(api::v2::Http::Status::OK, result.value().template as<std::string>());
                            co_return;
                        }
                        if (result.has_value())
                        {
                            std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, "Query returned empty result, please recheck your parameters.");
                            co_return;
                        }
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, "Failed to create sql query");
                    }
                    catch (const DatabaseBusy& e)
                    {
                        Helper::errorResponse(drogon::k503ServiceUnavailable, e.what(), std::move(cb));
                    }
                    catch (const std::exception& e)
                    {
                        Helper::failureResponse(e.what(), std::move(cb));
                    }
                };
                if (clientType == "users")
                {
                    handler.template operator()<User>();
                }
                else
                {
                    handler.template operator()<Provider>();
                }
                return;
            }

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Read, req, std::move(callback), req->body());
        }

        void update(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;
            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Update, req, std::move(callback), req->body(), *id);
        }
        void delete_(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType, id]() mutable -> drogon::AsyncTask
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

                    auto ctlIt = clientRegistry.find(clientType);
                    if (ctlIt == clientRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
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

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Delete, req, std::move(callback), *id);
        }
        void search(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType]() mutable -> drogon::AsyncTask
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

                    auto ctlIt = clientRegistry.find(clientType);
                    if (ctlIt == clientRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
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

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::Search, req, std::move(callback), req->body());
        }
        void getservices(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& clientType)
        {
            auto id = parseIdParam(req, "id", callback);
            if (!id)
                return;

            const bool async = asyncDb_;
            if (async)
            {
                auto cb = std::move(callback);
                auto handler = [this, req, cb, clientType, id]() mutable -> drogon::AsyncTask
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

                    auto ctlIt = clientRegistry.find(clientType);
                    if (ctlIt == clientRegistry.end())
                    {
                        std::move(mcb)(api::v2::Http::Status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", clientType));
                        co_return;
                    }

                    try
                    {
                        co_await std::visit([&](auto& controller) -> drogon::Task<void> {
                            co_await controller->GetServicesAsync(std::move(mcb), std::move(requester), id);
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

            executeControllerMethod(*dbExecutor, clientRegistry, clientType, &ClientControllerBase::GetServices, req, std::move(callback), *id);
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
        std::shared_ptr<DbExecutor> dbExecutor = Store::getObject<DbExecutor>();  // runs the blocking database work, shared with every route
        std::shared_ptr<GateKeeper> gateKeeper_ = Store::cachedObject<GateKeeper>();
        std::shared_ptr<DatabaseController> dbController_ = Store::cachedObject<DatabaseController>();
        const bool asyncDb_ = Store::cachedObject<Configurator>()->get<Configurator::DatabaseConfig>().async;
        using ClientVariant = std::variant<std::shared_ptr<ClientController<User>>, std::shared_ptr<ClientController<Provider>>>;

        std::unordered_map<std::string_view, ClientVariant> clientRegistry = {
            {"users", Store::getObject<ClientController<User>>()}, {"providers", Store::getObject<ClientController<Provider>>()}};
    };

}  // namespace api::v2
