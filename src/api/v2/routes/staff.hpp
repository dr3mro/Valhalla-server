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

//     using StaffVariant = std::variant<std::shared_ptr<StaffController<Clinics>>, std::shared_ptr<StaffController<Pharmacies>>,
//                                       std::shared_ptr<StaffController<Laboratories>>, std::shared_ptr<StaffController<RadiologyCenters>>>;

//     std::unordered_map<std::string_view, StaffVariant> staffRegistry = {{"clinics", Store::getObject<StaffController<Clinics>>()},
//                                                                         {"pharmacies", Store::getObject<StaffController<Pharmacies>>()},
//                                                                         {"laboratories", Store::getObject<StaffController<Laboratories>>()},
//                                                                         {"radiologycenters",
//                                                                         Store::getObject<StaffController<RadiologyCenters>>()}};