#include "api_v1_routes.hpp"

#include "utils/resthelper/resthelper.hpp"

// Define the base URL as a macro
#define PREFIX "/api/v1"
// Macro to concatenate the base URL with a path
#define URL(path) (PREFIX path)

API_V1_Routes::API_V1_Routes(std::shared_ptr<APP> &app)
{
    /////////////////// --------------- ROUTES--------------------------------------////////////////////

    ///////////////////---------------- Clients <Users/Providers>--------------------////////////////////

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Create, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/login"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authentication)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Authenticate, req, res,
                                        app->get_context<Authentication>(req).credentials);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/logout"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Deauthentication)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Logout, req, res,
                                        app->get_context<Deauthentication>(req).token);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/suspend"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Suspend, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/clients/<string>/activate"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Activate, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Read, req, res, app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Update, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, DataIntegrity)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view clientType)
            {
                executeControllerMethod(clientRegistry, clientType, &ClientControllerBase::Delete, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/clients/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
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
    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Create, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Read, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Update, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, DataIntegrity)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Delete, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(serviceRegistry, serviceName, &ServiceControllerBase::Search, req, res,
                                        app->get_context<Search>(req).search_json);
            });

    ///////////////////---------------- Clinics --------------------////////////////////
    CROW_ROUTE((*app), URL("/services/clinics/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ServiceControllerBase::Create, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/clinics/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ServiceControllerBase::Read, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/clinics/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ServiceControllerBase::Update, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/clinics/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, DataIntegrity)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(clinicRegistry, serviceName, &ServiceControllerBase::Delete, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/clinics/patients"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res)
            {
                executeControllerMethod(clinicRegistry, "patients", &ServiceControllerBase::Search, req, res,
                                        app->get_context<Search>(req).search_json);
            });

    CROW_ROUTE((*app), URL("/services/clinics/patients/getvisits"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res)
            {
                executeControllerMethod(clinicRegistry, "patients", &ServiceControllerBase::GetVisits, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    ////////////////////////////////////////////////////
    ///////////////////---------------- Appointments--------------------////////////////////
    CROW_ROUTE((*app), URL("/appointments/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::CreateAppointment, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/appointments/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest)
        .methods(crow::HTTPMethod::GET)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::ReadAppointment, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/appointments/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::PUT)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::UpdateAppointment, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/appointments/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, DataIntegrity)
        .methods(crow::HTTPMethod::DELETE)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::DeleteAppointment, req, res,
                                        app->get_context<XRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/appointments/<string>"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, XRequest, Search)
        .methods(crow::HTTPMethod::SEARCH)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
            {
                executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::SearchAppointment, req, res,
                                        app->get_context<Search>(req).search_json);
            });

    ////////////////////////////////////////////////////
    CROW_ROUTE((*app), URL("/services/<string>/staff/invite"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::InviteStaffToEntity, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/<string>/staff/add"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::AddStaffToEntity, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });

    CROW_ROUTE((*app), URL("/services/<string>/staff/remove"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, DataIntegrity, BRequest)
        .methods(crow::HTTPMethod::POST)(
            [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
            {
                executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::RemoveStaffFromEntity, req, res,
                                        app->get_context<BRequest>(req).criteria);
            });
    ///////////////////----------------
    /// HELLO--------------------////////////////////
    CROW_ROUTE((*app), URL("/hello"))
        .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime)
        .methods(crow::HTTPMethod::GET)(
            [](const crow::request &req, crow::response &res)
            {
                (void)req;
                jsoncons::json reply;
                reply["Message"] = "Welcome to ASGARD.";
                RestHelper::successResponse(res, crow::status::OK, reply.to_string());
            });

    // Catch-all route for unmatched paths
    CROW_ROUTE((*app), URL("/<string>"))
    (
        [](const crow::request &req, crow::response &res, const std::string_view path)
        {
            (void)req;
            RestHelper::errorResponse(res, crow::status::NOT_FOUND, fmt::format("{}/{} is not implemented yet.", PREFIX, path));
            return;
        });
}
