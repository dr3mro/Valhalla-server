#pragma once
#include <memory>

#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
#include "controllers/clientcontroller/clientcontroller.hpp"
#include "controllers/cliniccontroller/cliniccontroller.hpp"
#include "controllers/servicecontroller/servicecontroller.hpp"
#include "controllers/staffcontroller/staffcontroller.hpp"
#include "entities/appointments/clinic.hpp"
#include "entities/appointments/laboratory.hpp"
#include "entities/appointments/pharmacy.hpp"
#include "entities/appointments/radiologycenter.hpp"
#include "entities/people/provider.hpp"
#include "entities/people/user.hpp"
#include "entities/services/clinics/clinics.hpp"
#include "entities/services/clinics/patient/health.hpp"
#include "entities/services/clinics/patient/patientdrugs.hpp"
#include "entities/services/clinics/patient/reports.hpp"
#include "entities/services/clinics/visits/paidservices.hpp"
#include "entities/services/clinics/visits/prescriptions.hpp"
#include "entities/services/clinics/visits/requests.hpp"
#include "entities/services/clinics/visits/visitdrugs.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"
namespace api
{
    namespace v2
    {
        class Types
        {
           public:
            // Define variant type
            // using ServiceVariant =
            //     std::variant<std::shared_ptr<ServiceController<Clinics>>, std::shared_ptr<ServiceController<Pharmacies>>,
            //                  std::shared_ptr<ServiceController<Laboratories>>, std::shared_ptr<ServiceController<RadiologyCenters>>>;
            // using ClinicVariant = std::variant<std::shared_ptr<ClinicController<Patient>>, std::shared_ptr<ClinicController<Visits>>,
            //                                    std::shared_ptr<ClinicController<VisitDrugs>>, std::shared_ptr<ClinicController<Requests>>,
            //                                    std::shared_ptr<ClinicController<Prescriptions>>, std::shared_ptr<ClinicController<PaidServices>>,
            //                                    std::shared_ptr<ClinicController<Reports>>, std::shared_ptr<ClinicController<PatientDrugs>>,
            //                                    std::shared_ptr<ClinicController<Health>>>;

            // using AppointmentVariant =
            //     std::variant<std::shared_ptr<AppointmentController<ClinicAppointment>>,
            //     std::shared_ptr<AppointmentController<PharmacyAppointment>>,
            //                  std::shared_ptr<AppointmentController<LaboratoryAppointment>>,
            // std::shared_ptr < AppointmentController < RadiologyCenterAppointment >>> ;

            // using StaffVariant = std::variant<std::shared_ptr<StaffController<Clinics>>, std::shared_ptr<StaffController<Pharmacies>>,
            //                                   std::shared_ptr<StaffController<Laboratories>>, std::shared_ptr<StaffController<RadiologyCenters>>>;

            // Type mapping to determine the correct type from the string
            // Map for type mapping
            // Service handlers map
            // std::unordered_map<std::string_view, ServiceVariant> serviceRegistry = {
            //     {"clinics", Store::getObject<ServiceController<Clinics>>()},
            //     {"pharmacies", Store::getObject<ServiceController<Pharmacies>>()},
            //     {"laboratories", Store::getObject<ServiceController<Laboratories>>()},
            //     {"radiologycenters", Store::getObject<ServiceController<RadiologyCenters>>()}};

            // std::unordered_map<std::string_view, ClinicVariant> clinicRegistry = {
            //     {"patients", Store::getObject<ClinicController<Patient>>()},
            //     {"visits", Store::getObject<ClinicController<Visits>>()},
            //     {"visitDrugs", Store::getObject<ClinicController<VisitDrugs>>()},
            //     {"requests", Store::getObject<ClinicController<Requests>>()},
            //     {"prescriptions", Store::getObject<ClinicController<Prescriptions>>()},
            //     {"paidservices", Store::getObject<ClinicController<PaidServices>>()},
            //     {"reports", Store::getObject<ClinicController<Reports>>()},
            //     {"patientdrugs", Store::getObject<ClinicController<PatientDrugs>>()},
            //     {"health", Store::getObject<ClinicController<Health>>()}};

            // std::unordered_map<std::string_view, AppointmentVariant> appointmentRegistry = {
            //     {"clinics", Store::getObject<AppointmentController<ClinicAppointment>>()},
            //     {"pharmacies", Store::getObject<AppointmentController<PharmacyAppointment>>()},
            //     {"laboratories", Store::getObject<AppointmentController<LaboratoryAppointment>>()},
            //     {"radiologycenters", Store::getObject<AppointmentController<RadiologyCenterAppointment>>()}};

            // std::unordered_map<std::string_view, StaffVariant> staffRegistry = {
            //     {"clinics", Store::getObject<StaffController<Clinics>>()},
            //     {"pharmacies", Store::getObject<StaffController<Pharmacies>>()},
            //     {"laboratories", Store::getObject<StaffController<Laboratories>>()},
            //     {"radiologycenters", Store::getObject<StaffController<RadiologyCenters>>()}};
        };

    }  // namespace v2

}  // namespace api