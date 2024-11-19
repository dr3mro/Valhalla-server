// #pragma once
// #include <crow.h>
// #include <crow/middlewares/cors.h>

// #include <memory>

// #include "controllers/appointmentcontroller/appointmentcontroller.hpp"
// #include "controllers/clientcontroller/clientcontroller.hpp"
// #include "controllers/cliniccontroller/cliniccontroller.hpp"
// #include "controllers/servicecontroller/servicecontroller.hpp"
// #include "controllers/staffcontroller/staffcontroller.hpp"
// #include "entities/appointments/clinic.hpp"
// #include "entities/appointments/laboratory.hpp"
// #include "entities/appointments/pharmacy.hpp"
// #include "entities/appointments/radiologycenter.hpp"
// #include "entities/people/provider.hpp"
// #include "entities/people/user.hpp"
// #include "entities/services/clinics/clinics.hpp"
// #include "entities/services/clinics/patient/health.hpp"
// #include "entities/services/clinics/patient/patient.hpp"
// #include "entities/services/clinics/patient/patientdrugs.hpp"
// #include "entities/services/clinics/patient/reports.hpp"
// #include "entities/services/clinics/visits/paidservices.hpp"
// #include "entities/services/clinics/visits/prescriptions.hpp"
// #include "entities/services/clinics/visits/requests.hpp"
// #include "entities/services/clinics/visits/visitdrugs.hpp"
// #include "entities/services/clinics/visits/visits.hpp"
// #include "entities/services/laboratories.hpp"
// #include "entities/services/pharmacies.hpp"
// #include "entities/services/radiologycenters.hpp"
// #include "middlewares/authentication.hpp"
// #include "middlewares/authorization.hpp"
// #include "middlewares/brequest.hpp"
// #include "middlewares/dataintegrity.hpp"
// #include "middlewares/elapsedtime.hpp"
// #include "middlewares/pathparameter.hpp"
// #include "middlewares/ratelimit.hpp"
// #include "middlewares/search.hpp"
// #include "middlewares/xrequest.hpp"
// #include "utils/ahelper/helper.hpp"

// using APP =
//     crow::App<crow::CORSHandler, RateLimit, ElapsedTime, PathParameter, XRequest, BRequest, Authentication, Authorization, Search, DataIntegrity>;

// class API_V1_Routes
// {
//    public:
//     API_V1_Routes(std::shared_ptr<APP>& app);
//     virtual ~API_V1_Routes() = default;

//    private:
//     // Define variant type

//     using ClinicVariant = std::variant<std::shared_ptr<ClinicController<Patient>>, std::shared_ptr<ClinicController<Visits>>,
//                                        std::shared_ptr<ClinicController<VisitDrugs>>, std::shared_ptr<ClinicController<Requests>>,
//                                        std::shared_ptr<ClinicController<Prescriptions>>, std::shared_ptr<ClinicController<PaidServices>>,
//                                        std::shared_ptr<ClinicController<Reports>>, std::shared_ptr<ClinicController<PatientDrugs>>,
//                                        std::shared_ptr<ClinicController<Health>>>;

//     using AppointmentVariant =
//         std::variant<std::shared_ptr<AppointmentController<ClinicAppointment>>, std::shared_ptr<AppointmentController<PharmacyAppointment>>,
//                      std::shared_ptr<AppointmentController<LaboratoryAppointment>>,
//                      std::shared_ptr<AppointmentController<RadiologyCenterAppointment>>>;

//     using ClientVariant = std::variant<std::shared_ptr<ClientController<User>>, std::shared_ptr<ClientController<Provider>>>;

//     using StaffVariant = std::variant<std::shared_ptr<StaffController<Clinics>>, std::shared_ptr<StaffController<Pharmacies>>,
//                                       std::shared_ptr<StaffController<Laboratories>>, std::shared_ptr<StaffController<RadiologyCenters>>>;

//     // Type mapping to determine the correct type from the string
//     // Map for type mapping
//     // Service handlers map

//     std::unordered_map<std::string_view, ClinicVariant> clinicRegistry = {{"patients", Store::getObject<ClinicController<Patient>>()},
//                                                                           {"visits", Store::getObject<ClinicController<Visits>>()},
//                                                                           {"visitDrugs", Store::getObject<ClinicController<VisitDrugs>>()},
//                                                                           {"requests", Store::getObject<ClinicController<Requests>>()},
//                                                                           {"prescriptions", Store::getObject<ClinicController<Prescriptions>>()},
//                                                                           {"paidservices", Store::getObject<ClinicController<PaidServices>>()},
//                                                                           {"reports", Store::getObject<ClinicController<Reports>>()},
//                                                                           {"patientdrugs", Store::getObject<ClinicController<PatientDrugs>>()},
//                                                                           {"health", Store::getObject<ClinicController<Health>>()}};

//     std::unordered_map<std::string_view, AppointmentVariant> appointmentRegistry = {
//         {"clinics", Store::getObject<AppointmentController<ClinicAppointment>>()},
//         {"pharmacies", Store::getObject<AppointmentController<PharmacyAppointment>>()},
//         {"laboratories", Store::getObject<AppointmentController<LaboratoryAppointment>>()},
//         {"radiologycenters", Store::getObject<AppointmentController<RadiologyCenterAppointment>>()}};

//     std::unordered_map<std::string_view, ClientVariant> clientRegistry = {{"users", Store::getObject<ClientController<User>>()},
//                                                                           {"providers", Store::getObject<ClientController<Provider>>()}};

//     std::unordered_map<std::string_view, StaffVariant> staffRegistry = {{"clinics", Store::getObject<StaffController<Clinics>>()},
//                                                                         {"pharmacies", Store::getObject<StaffController<Pharmacies>>()},
//                                                                         {"laboratories", Store::getObject<StaffController<Laboratories>>()},
//                                                                         {"radiologycenters",
//                                                                         Store::getObject<StaffController<RadiologyCenters>>()}};

//     template <typename Func, typename Registry, typename... Args>
//     void executeControllerMethod(const Registry& registry, const std::string_view key, Func method, const crow::request& req, crow::response& res,
//                                  Args&&... args)
//     {
//         auto it = registry.find(key);
//         if (it != registry.end())
//         {
//             try
//             {
//                 std::visit(
//                     [&](const auto& controller)
//                     {
//                         // Use std::invoke on the dereferenced controller object
//                         std::invoke(method, controller.get(), req, res, std::forward<Args>(args)...);
//                     },
//                     it->second);
//             }
//             catch (const std::exception& e)
//             {
//                 RestHelper::failureResponse(res, e.what());
//             }
//         }
//         else
//         {
//             RestHelper::errorResponse(res, crow::status::BAD_REQUEST, fmt::format("Type mapping not found for: {}", key));
//         }
//     }
// };
