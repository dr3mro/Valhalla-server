#include "api_v1_routes.hpp"

#include "crow/common.h"
#include "utils/resthelper/resthelper.hpp"

// Define the base URL as a macro
#define PREFIX "/api/v1"
// Macro to concatenate the base URL with a path
#define URL(path) (PREFIX path)

API_V1_Routes::API_V1_Routes(std::shared_ptr<APP> &app)
{
    /////////////////// --------------- ROUTES--------------------------------------////////////////////

    ///////////////////---------------- Clients <Users/Providers>--------------------////////////////////

    CROW_ROUTE((*app), URL("/clients/<string>/create"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Create, req, res, app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/login"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authentication)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Login, req, res,
                                        app->get_context<Authentication>(req).credentials);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/logout"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Logout, req, res,
                                        app->get_context<Authorization>(req).userInfo.token.value());
            });

    CROW_ROUTE((*app), URL("/clients/<string>/suspend"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Suspend, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/activate"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Activate, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/read"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            { executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Read, req, res, app->get_context<BRequest>(req).payload); });

    CROW_ROUTE((*app), URL("/clients/<string>/update"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Update, req, res, app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/delete"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Delete, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/search"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, Search)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Search, req, res,
                                        app->get_context<Search>(req).search_json);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/services"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::GetServices, req, res,
                                        app->get_context<Authorization>(req).userInfo.userID);
            });

    ///////////////////---------------- Service--------------------////////////////////
    CROW_ROUTE((*app), URL("/services/<string>/create"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Create, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/read"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Read, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/update"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Update, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/delete"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Delete, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    CROW_ROUTE((*app), URL("/services/<string>/search"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, Search)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Search, req, res,
                                        app->get_context<Search>(req).search_json);
            });

    ///////////////////---------------- Clinics --------------------////////////////////
    CROW_ROUTE((*app), URL("/services/clinics/<string>/create"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Create, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/clinics/<string>/read"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Read, req, res, app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/clinics/<string>/update"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Update, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/clinics/<string>/delete"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Delete, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    CROW_ROUTE((*app), URL("/services/clinics/patients/search"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, Search)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res)
            {
                executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::Search, req, res,
                                        app->get_context<Search>(req).search_json);
            });

    CROW_ROUTE((*app), URL("/services/clinics/patients/getvisits"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res)
            {
                executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::GetVisits, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    ////////////////////////////////////////////////////
    ///////////////////---------------- Appointments--------------------////////////////////
    CROW_ROUTE((*app), URL("/services/<string>/appointments/create"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Create, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/appointments/read"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Read, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/appointments/update"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Update, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/appointments/delete"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Delete, req, res,
                                        app->get_context<PathParameter>(req).params);
            });

    ////////////////////////////////////////////////////
    CROW_ROUTE((*app), URL("/services/<string>/staff/invite"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::InviteStaffToEntity, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/staff/add"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::AddStaffToEntity, req, res,
                                        app->get_context<BRequest>(req).payload);
            });

    CROW_ROUTE((*app), URL("/services/<string>/staff/remove"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::RemoveStaffFromEntity, req, res,
                                        app->get_context<BRequest>(req).payload);
            });
    ///////////////////----------------
    /// HELLO--------------------////////////////////
    CROW_ROUTE((*app), URL("/hello"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime)
        .methods(crow::HTTPMethod::GET)(
            [](const crow::request &req, crow::response &res)
            {
                (void)req;
                RestHelper::successResponseJsoned(res, crow::status::OK, "Welcome to ASGARD.");
            });

    // Catch-all route for unmatched paths
    CROW_ROUTE((*app), "/<path>")
    (
        [](const crow::request &req, crow::response &res, const std::string_view path)
        {
            RestHelper::errorResponse(
                res, crow::status::NOT_FOUND,
                fmt::format("The endpoint ({}) is not implemented yet with method {}, Please read the manual.", path, crow::method_name(req.method)));
            return;
        });
}
