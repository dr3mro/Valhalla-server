#include "objectfactory.hpp"

#include "configurator/configurator.hpp"
#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
#include "controllers/clientcontroller/clientcontroller.hpp"
#include "controllers/cliniccontroller/cliniccontroller.hpp"
#include "controllers/databasecontroller/databasecontroller.hpp"
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
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/patient/patientdrugs.hpp"
#include "entities/services/clinics/patient/reports.hpp"
#include "entities/services/clinics/visits/paidservices.hpp"
#include "entities/services/clinics/visits/prescriptions.hpp"
#include "entities/services/clinics/visits/requests.hpp"
#include "entities/services/clinics/visits/visitdrugs.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "entities/services/laboratories.hpp"
#include "entities/services/pharmacies.hpp"
#include "entities/services/radiologycenters.hpp"
#include "store/store.hpp"
#include "utils/Logger/logger.hpp"
#include "utils/databaseschema/databaseschema.hpp"
#include "utils/dosdetector/dosdetector.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"

ObjectFactory::ObjectFactory()
{
    Store::registerObject<Configurator>();
    Store::registerObject<Logger>();
    Store::registerObject<MemCache<api::v2::Types::ClientLoginData>>(4096, std::chrono::seconds(3600));
    Store::registerObject<DatabaseConnectionPool>();
    Store::registerObject<DatabaseController>();
    Store::registerObject<PasswordCrypt>();
    Store::registerObject<SessionManager>();
    Store::registerObject<TokenManager>();
    Store::registerObject<GateKeeper>();
    Store::registerObject<ClinicController<Health>>();
    Store::registerObject<ClinicController<PatientDrugs>>();
    Store::registerObject<ClinicController<Reports>>();
    Store::registerObject<ClinicController<PaidServices>>();
    Store::registerObject<ClinicController<Prescriptions>>();
    Store::registerObject<ClinicController<Requests>>();
    Store::registerObject<ClinicController<VisitDrugs>>();
    Store::registerObject<ClinicController<Visits>>();
    Store::registerObject<ClinicController<Patient>>();
    Store::registerObject<ServiceController<Clinics>>();
    Store::registerObject<ServiceController<Pharmacies>>();
    Store::registerObject<ServiceController<Laboratories>>();
    Store::registerObject<ServiceController<RadiologyCenters>>();
    Store::registerObject<AppointmentController<ClinicAppointment>>();
    Store::registerObject<AppointmentController<PharmacyAppointment>>();
    Store::registerObject<AppointmentController<LaboratoryAppointment>>();
    Store::registerObject<AppointmentController<RadiologyCenterAppointment>>();
    Store::registerObject<ClientController<Provider>>();
    Store::registerObject<ClientController<User>>();
    Store::registerObject<DOSDetector>();
    Store::registerObject<StaffController<Clinics>>();
    Store::registerObject<StaffController<Pharmacies>>();
    Store::registerObject<StaffController<Laboratories>>();
    Store::registerObject<StaffController<RadiologyCenters>>();
    Store::registerObject<DatabaseSchema>();
}
