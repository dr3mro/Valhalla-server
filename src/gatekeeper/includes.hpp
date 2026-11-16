/**
 * @file includes.hpp
 * @brief Umbrella header that makes every authorizable entity type complete for
 * GateKeeper template instantiations.
 * @details GateKeeper's permission-check helpers are templates, which means the
 * compiler must see the full definition of each entity type before it can
 * generate the right code. This header gathers all those entity definitions in
 * one place. The IWYU pragmas on each line tell the "include-what-you-use"
 * cleanup tool that these includes are intentional (they are needed by
 * gatekeeper.cpp, even though this file does not mention the types itself). No
 * functions or data live here; it is purely a list of headers so that call
 * sites and the implementation file do not each repeat the whole entity graph.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This file supports the security layer. GateKeeper offers permission helpers
 * like canRead<T>() and canDelete<T>() that work for many entity types (users,
 * clinics, visits, appointments, and so on). In gatekeeper.cpp those helpers
 * are explicitly instantiated once per supported type, which requires every
 * type to be "complete" (fully defined) at that point. This header provides
 * exactly that, so a new entity can be supported by adding one line here plus
 * one instantiation in gatekeeper.cpp. Without it, gatekeeper.cpp would have to
 * include twenty headers itself, and forgetting one would cause a confusing
 * template compile error.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Header / umbrella header: https://en.wikipedia.org/wiki/Include_guard
 * - Template (compile-time generics):
 * https://en.cppreference.com/w/cpp/language/templates
 * - Explicit instantiation:
 * https://en.cppreference.com/w/cpp/language/templates
 * - Complete type (why the full definition is needed):
 * https://en.cppreference.com/w/cpp/language/classes
 * - Include-what-you-use tool:
 * https://github.com/include-what-you-use/include-what-you-use
 *
 * @note No runtime code lives here; this file exists solely for template
 * visibility.
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // The compiler includes this file at most once per translation
              // unit, avoiding duplicate definitions.

// Each line below pulls in the full definition of one entity type. The trailing
// "IWYU pragma: keep" marks the include as deliberate so automatic cleanup
// tools do not delete it just because this header does not name the type
// directly.
// IWYU pragma: keep  // Defines ClinicAppointment, a clinic booking record.
#include "entities/appointments/clinic.hpp"
// IWYU pragma: keep  // Defines LaboratoryAppointment, a lab booking record.
#include "entities/appointments/laboratory.hpp"
// IWYU pragma: keep  // Defines PharmacyAppointment, a pharmacy booking record.
#include "entities/appointments/pharmacy.hpp"
// IWYU pragma: keep  // Defines RadiologyCenterAppointment, an imaging booking
// record.
#include "entities/appointments/radiologycenter.hpp"
// IWYU pragma: keep  // Defines Provider, a staff/client who can be authorized
// (a Client type).
#include "entities/people/provider.hpp"
// IWYU pragma: keep  // Defines User, a normal client who can be authorized (a
// Client type).
#include "entities/people/user.hpp"
// IWYU pragma: keep  // Defines Clinics, a service that can have staff managed.
#include "entities/services/clinics/clinics.hpp"
// IWYU pragma: keep  // Defines Health, a patient health record with CRUD
// checks.
#include "entities/services/clinics/patient/health.hpp"
// IWYU pragma: keep  // Defines Patient, a patient record with CRUD checks.
#include "entities/services/clinics/patient/patient.hpp"
// IWYU pragma: keep  // Defines PatientDrugs, drugs tied to a patient.
#include "entities/services/clinics/patient/patientdrugs.hpp"
// IWYU pragma: keep  // Defines Reports, medical reports for a patient.
#include "entities/services/clinics/patient/reports.hpp"
// IWYU pragma: keep  // Defines PaidServices, services paid for during a visit.
#include "entities/services/clinics/visits/paidservices.hpp"
// IWYU pragma: keep  // Defines Prescriptions issued during a visit.
#include "entities/services/clinics/visits/prescriptions.hpp"
// IWYU pragma: keep  // Defines Requests made during a visit.
#include "entities/services/clinics/visits/requests.hpp"
// IWYU pragma: keep  // Defines VisitDrugs, drugs given during a visit.
#include "entities/services/clinics/visits/visitdrugs.hpp"
// IWYU pragma: keep  // Defines Visits, a patient visit record.
#include "entities/services/clinics/visits/visits.hpp"
// IWYU pragma: keep  // Defines Laboratories, a service that can have staff
// managed.
#include "entities/services/laboratories.hpp"
// IWYU pragma: keep  // Defines Pharmacies, a service that can have staff
// managed.
#include "entities/services/pharmacies.hpp"
// IWYU pragma: keep  // Defines RadiologyCenters, a service that can have staff
// managed.
#include "entities/services/radiologycenters.hpp"
