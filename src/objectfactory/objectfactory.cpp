#include "objectfactory.hpp"

#include "api/v2/basic/common.hpp"
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
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"
ObjectFactory::ObjectFactory()
{
    Store::registerObject<Configurator>();
    Store::registerObject<Logger>();
    Store::registerObject<DatabaseConnectionPool>();
    Store::registerObject<DatabaseController>();
    Store::registerObject<SessionManager>();
    Store::registerObject<TokenManager>();
    Store::registerObject<PasswordCrypt>();
    Store::registerObject<ClinicController<Health, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<PatientDrugs, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<Reports, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<PaidServices, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<Prescriptions, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<Requests, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<VisitDrugs, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<Visits, CALLBACKSIGNATURE>>();
    Store::registerObject<ClinicController<Patient, CALLBACKSIGNATURE>>();
    Store::registerObject<ServiceController<Clinics, CALLBACKSIGNATURE>>();
    Store::registerObject<ServiceController<Pharmacies, CALLBACKSIGNATURE>>();
    Store::registerObject<ServiceController<Laboratories, CALLBACKSIGNATURE>>();
    Store::registerObject<ServiceController<RadiologyCenters, CALLBACKSIGNATURE>>();
    Store::registerObject<AppointmentController<ClinicAppointment, CALLBACKSIGNATURE>>();
    Store::registerObject<AppointmentController<PharmacyAppointment, CALLBACKSIGNATURE>>();
    Store::registerObject<AppointmentController<LaboratoryAppointment, CALLBACKSIGNATURE>>();
    Store::registerObject<AppointmentController<RadiologyCenterAppointment, CALLBACKSIGNATURE>>();
    Store::registerObject<ClientController<Provider, CALLBACKSIGNATURE>>();
    Store::registerObject<ClientController<User, CALLBACKSIGNATURE>>();
    Store::registerObject<DOSDetector>();
    Store::registerObject<StaffController<Clinics, CALLBACKSIGNATURE>>();
    Store::registerObject<StaffController<Pharmacies, CALLBACKSIGNATURE>>();
    Store::registerObject<StaffController<Laboratories, CALLBACKSIGNATURE>>();
    Store::registerObject<StaffController<RadiologyCenters, CALLBACKSIGNATURE>>();
    Store::registerObject<DatabaseSchema>();
}
