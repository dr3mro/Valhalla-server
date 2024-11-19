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

//     using ClinicVariant = std::variant<std::shared_ptr<ClinicController<Patient>>, std::shared_ptr<ClinicController<Visits>>,
//                                        std::shared_ptr<ClinicController<VisitDrugs>>, std::shared_ptr<ClinicController<Requests>>,
//                                        std::shared_ptr<ClinicController<Prescriptions>>, std::shared_ptr<ClinicController<PaidServices>>,
//                                        std::shared_ptr<ClinicController<Reports>>, std::shared_ptr<ClinicController<PatientDrugs>>,
//                                        std::shared_ptr<ClinicController<Health>>>;

//     std::unordered_map<std::string_view, ClinicVariant> clinicRegistry = {{"patients", Store::getObject<ClinicController<Patient>>()},
//                                                                           {"visits", Store::getObject<ClinicController<Visits>>()},
//                                                                           {"visitDrugs", Store::getObject<ClinicController<VisitDrugs>>()},
//                                                                           {"requests", Store::getObject<ClinicController<Requests>>()},
//                                                                           {"prescriptions", Store::getObject<ClinicController<Prescriptions>>()},
//                                                                           {"paidservices", Store::getObject<ClinicController<PaidServices>>()},
//                                                                           {"reports", Store::getObject<ClinicController<Reports>>()},
//                                                                           {"patientdrugs", Store::getObject<ClinicController<PatientDrugs>>()},
//                                                                           {"health", Store::getObject<ClinicController<Health>>()}};