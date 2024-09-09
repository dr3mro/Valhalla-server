#include "api_v1_routes.hpp"

#include <functional>

// Define the base URL as a macro
#define PREFIX "/api/v1"
// Macro to concatenate the base URL with a path
#define URL(path) (PREFIX path)

API_V1_Routes::API_V1_Routes(std::shared_ptr<APP> &app)
{
    /////////////////// --------------- ROUTES--------------------------------------////////////////////

    ///////////////////---------------- Clients <Users/Providers>--------------------////////////////////

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this](const crow::request &req, crow::response &res, const std::string_view clientType)
            { executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::CreateClient, std::cref(req), std::ref(res)); });

    CROW_ROUTE((*app), URL("/clients/<string>/login"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authentication)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::AuthenticateClient, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<Authentication>(std::cref(req)).credentials));
            });

    CROW_ROUTE((*app), URL("/clients/<string>/logout"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Deauthentication)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::LogoutClient, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<Deauthentication>(std::cref(req)).token));
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::ReadClient, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<XRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity)
        .methods(crow::HTTPMethod::PUT)(
            [this](const crow::request &req, crow::response &res, const std::string_view clientType)
            { executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::UpdateClient, std::cref(req), std::ref(res)); });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, DataIntegrity)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::DeleteClient, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<XRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeServiceMethod(clientRegistry, clientType, &ClientControllerBase::SearchClient, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<Search>(std::cref(req)).search_json));
            });

    ///////////////////---------------- Service--------------------////////////////////
    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(serviceRegistry, serviceName, &ServiceControllerBase::CreateService, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<BRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(serviceRegistry, serviceName, &ServiceControllerBase::ReadService, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<XRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(serviceRegistry, serviceName, &ServiceControllerBase::UpdateService, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<BRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, DataIntegrity)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(serviceRegistry, serviceName, &ServiceControllerBase::DeleteService, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<XRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(serviceRegistry, serviceName, &ServiceControllerBase::SearchService, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<Search>(std::cref(req)).search_json));
            });

    ////////////////////////////////////////////////////
    CROW_ROUTE((*app), URL("/services/<string>/staff/invite"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(staffRegistry, serviceName, &StaffControllerBase::InviteStaffToEntity, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<BRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/services/<string>/staff/add"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(staffRegistry, serviceName, &StaffControllerBase::AddStaffToEntity, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<BRequest>(std::cref(req)).criteria));
            });

    CROW_ROUTE((*app), URL("/services/<string>/staff/remove"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeServiceMethod(staffRegistry, serviceName, &StaffControllerBase::RemoveStaffFromEntity, std::cref(req), std::ref(res),
                                     std::cref(app->get_context<BRequest>(std::cref(req)).criteria));
            });
    ///////////////////----------------
    /// HELLO--------------------////////////////////
    CROW_ROUTE((*app), URL("/hello"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime)
        .methods(crow::HTTPMethod::GET)(
            [](const crow::request &req, crow::response &res)
            {
                (void)req;
                res.code = crow::OK;
                res.end("{\n\"Message\" : \"Welcome to ASGARD.\"\n}");
            });
}
