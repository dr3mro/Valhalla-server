

// ///////////////////---------------- Clinics --------------------////////////////////
// CROW_ROUTE((*app), URL("/services/clinics/<string>/create"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Create, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/clinics/<string>/read"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Read, req, res,
//             app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/clinics/<string>/update"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::PUT)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Update, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/clinics/<string>/delete"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
//     .methods(crow::HTTPMethod::DELETE)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(clinicRegistry, serviceName, &ClinicControllerBase::Delete, req, res,
//                                     app->get_context<PathParameter>(req).params);
//         });

// CROW_ROUTE((*app), URL("/services/clinics/patients/search"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, Search)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res)
//         {
//             executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::Search, req, res,
//                                     app->get_context<Search>(req).search_json);
//         });

// CROW_ROUTE((*app), URL("/services/clinics/patients/getvisits"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
//     .methods(crow::HTTPMethod::GET)(
//         [this, app](const crow::request &req, crow::response &res)
//         {
//             executeControllerMethod(clinicRegistry, "patients", &ClinicControllerBase::GetVisits, req, res,
//                                     app->get_context<PathParameter>(req).params);
//         });

// ////////////////////////////////////////////////////
// ///////////////////---------------- Appointments--------------------////////////////////
// CROW_ROUTE((*app), URL("/services/<string>/appointments/create"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
//         {
//             executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Create, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/<string>/appointments/read"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
//         {
//             executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Read, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/<string>/appointments/update"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::PUT)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
//         {
//             executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Update, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/<string>/appointments/delete"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, PathParameter)
//     .methods(crow::HTTPMethod::DELETE)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view entityType)
//         {
//             executeControllerMethod(appointmentRegistry, entityType, &AppointmentControllerBase::Delete, req, res,
//                                     app->get_context<PathParameter>(req).params);
//         });

// ////////////////////////////////////////////////////
// CROW_ROUTE((*app), URL("/services/<string>/staff/invite"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::InviteStaffToEntity, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/<string>/staff/add"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::AddStaffToEntity, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });

// CROW_ROUTE((*app), URL("/services/<string>/staff/remove"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime, Authorization, BRequest, DataIntegrity)
//     .methods(crow::HTTPMethod::POST)(
//         [this, app](const crow::request &req, crow::response &res, const std::string_view serviceName)
//         {
//             executeControllerMethod(staffRegistry, serviceName, &StaffControllerBase::RemoveStaffFromEntity, req, res,
//                                     app->get_context<BRequest>(req).payload);
//         });
// ///////////////////----------------
// /// HELLO--------------------////////////////////
// CROW_ROUTE((*app), URL("/hello"))
//     .CROW_MIDDLEWARES(*app, RateLimit, ElapsedTime)
//     .methods(crow::HTTPMethod::GET)(
//         [](const crow::request &req, crow::response &res)
//         {
//             (void)req;
//             RestHelper::successResponse(res, RestHelper::stringify(RestHelper::jsonify("Welcome to ASGARD.")));
//         });

// // Catch-all route for unmatched paths
// CROW_ROUTE((*app), "/<path>")
//     .methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST, crow::HTTPMethod::PUT, crow::HTTPMethod::DELETE, crow::HTTPMethod::PATCH,
//              crow::HTTPMethod::OPTIONS, crow::HTTPMethod::HEAD)  // All HTTP methods
//     (
//         [](const crow::request &req, crow::response &res, const std::string_view path)
//         {
//             RestHelper::errorResponse(res, crow::status::NOT_FOUND,
//                                       fmt::format("The endpoint ({}) is not implemented yet with method {}, Please read the manual.", path,
//                                                   crow::method_name(req.method)));
//             return;
//         });
// }
