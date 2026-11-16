/**
 * @file permissionmanager.hpp
 * @brief The single place that answers "may this person do this thing to this
 * record?". (Originally: "Central authorization facade: answers "may this
 * requester perform this operation on this entity?".")
 * @details This header declares PermissionManager, a class full of `canCreate`,
 *          `canRead`, `canUpdate` and `canDelete` checks. Each check is written
 *          once as a template and works for several entity types. Every check
 *          also has an `Async` coroutine twin used by the non-blocking routes.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers call these checks before changing or returning data, so this is
 * the gate that stops one client reading another client's records. It decides
 * who may manage staff, view services, and edit cases or appointments. Without
 * it, authorization rules would be scattered and easy to get wrong.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Authorization vs authentication:
 * https://en.wikipedia.org/wiki/Authorization
 * - C++ templates: https://en.cppreference.com/w/cpp/language/templates
 * - Concepts (constraints on template arguments):
 * https://en.cppreference.com/w/cpp/language/constraints
 * - Coroutines in C++: https://en.cppreference.com/w/cpp/language/coroutines
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <cstdint>  // uint64_t, the type of all entity ids.
// jsoncons::json for optional request payloads.
#include <jsoncons/basic_json.hpp>
#include <optional>  // std::optional for a payload that may be absent.

// drogon::Task, the coroutine return type of the Async checks.
#include "drogon/utils/coroutine.h"
// Client_t/Service_t/Case_t/Appointment_t, the template constraints.
#include "utils/global/concepts.hpp"
// Http::Error, used to report why a request was denied.
#include "utils/global/http.hpp"
// Requester, the authenticated caller whose rights are checked.
#include "utils/global/requester.hpp"
namespace api::v2  // All API version-2 code lives in this namespace.
{
  /**
   * @class PermissionManager
   * @brief Central authorization facade: answers "may this requester perform
   * this operation on this entity?".
   * @details Overloads are grouped by the concept constraining the entity
   * type (Client_t, Service_t, Case_t, Appointment_t). Most checks load the
   * entity's permission JSON (through
   * PermissionManagerPrivate::getPermissionsOfEntity) and apply the service
   *          owner/admin/staff rules; the plain client checks only assert
   * that the requester's group and id match the target client. Every `*Async`
   * overload is the coroutine counterpart of a synchronous check; those
   * needing permission data use the async (database/cached) lookup. On
   * denial, @p error receives a status and reason.
   * @note Each overload is explicitly instantiated for the supported entity
   * types at the bottom of permissionmanager.cpp; this header only declares
   * them. In everyday words: think of this class as a receptionist who checks
   *       ID before letting someone do something. It does not fetch data
   * itself; it asks PermissionManagerPrivate for the facts and then decides.
   */
  class PermissionManager  // A facade: one simple entry point hiding the
                           // detailed checks.
  {
   public:
    PermissionManager() = default;  // No state to set up, so the
                                    // compiler-generated constructor is fine.
    PermissionManager(const PermissionManager&) =
        default;  // Copying is harmless; the class holds no resources.
    PermissionManager& operator=(const PermissionManager&) =
        default;  // Copy assignment is harmless for the same reason.
    PermissionManager(PermissionManager&&) =
        default;  // Moving is harmless; there is nothing to transfer.
    PermissionManager& operator=(PermissionManager&&) =
        default;  // Move assignment is harmless for the same reason.
    virtual ~PermissionManager() =
        default;  // Virtual destructor so cleanup is correct if used
                  // through a base pointer.

    /**
     * @brief Client creation is unrestricted (always allowed).
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller (unused).
     * @param[in] data_j Optional JSON payload (unused).
     * @param[out] error Denial details (unused).
     * @return Always `true`.
     */
    // client is either user or provider: for these, a person may only touch
    // their own record.
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    bool canCreate(const Requester&                     requester,
                   const std::optional<jsoncons::json>& data_j,
                   Http::Error& error);  // Synchronous create check; true when
                                         // the action is allowed.

    /** @brief Coroutine form of canCreate() for clients; always returns
     * `true`. */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    drogon::Task<bool> canCreateAsync(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& data_j,
        Http::Error& error);  // Coroutine create check; co_await it from an
                              // async route.

    /**
     * @brief Allows reading a client only if requester group and id match
     * the target.
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] client_id Id of the client being accessed.
     * @param[out] error Receives denial details.
     * @return True when both group and id match.
     */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    bool canRead(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error&     error);  // Synchronous read check for a client record.

    /** @brief Coroutine form of canRead() for clients. */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    drogon::Task<bool> canReadAsync(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error&     error);  // Coroutine read check for a client record.

    /**
     * @brief Allows updating a client only if requester group and id match
     * the target.
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] client_id Id of the client being accessed.
     * @param[out] error Receives denial details.
     * @return True when both group and id match.
     */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    bool canUpdate(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error& error);  // Synchronous update check for a client record.

    /** @brief Coroutine form of canUpdate() for clients. */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    drogon::Task<bool> canUpdateAsync(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error&     error);  // Coroutine update check for a client record.

    /**
     * @brief Allows deleting a client only if requester group and id match
     * the target.
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] client_id Id of the client being accessed.
     * @param[out] error Receives denial details.
     * @return True when both group and id match.
     */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    bool canDelete(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error& error);  // Synchronous delete check for a client record.

    /** @brief Coroutine form of canDelete() for clients. */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    drogon::Task<bool> canDeleteAsync(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error&     error);  // Coroutine delete check for a client record.

    /**
     * @brief Allows toggling a client's active flag only on a matching
     * group and id.
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] client_id Id of the client whose status is targeted.
     * @param[out] error Receives denial details.
     * @return True when both group and id match.
     */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    bool canToggleActive(const Requester& requester,
                         uint64_t         client_id,
                         Http::Error& error);  // Synchronous check for changing
                                               // a client's active flag.

    /** @brief Coroutine form of canToggleActive() for clients. */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    drogon::Task<bool> canToggleActiveAsync(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error&
            error);  // Coroutine check for changing a client's active flag.

    /**
     * @brief Allows listing a client's services only on a matching group
     * and id.
     * @tparam T Client type, constrained by Client_t.
     * @param[in] requester Authenticated caller.
     * @param[in] client_id Id of the client whose services are requested.
     * @param[out] error Receives denial details.
     * @return True when both group and id match.
     */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    bool canGetServices(const Requester& requester,
                        uint64_t         client_id,
                        Http::Error& error);  // Synchronous check for listing a
                                              // client's services.

    /** @brief Coroutine form of canGetServices() for clients. */
    template <
        Client_t T>  // Restricts this overload to client types (User/Provider).
    drogon::Task<bool> canGetServicesAsync(
        const Requester& requester,
        uint64_t         client_id,
        Http::Error&
            error);  // Coroutine check for listing a client's services.

    // Service is either a clinic, pharmacy, lab or rad center: these checks
    // use the service's permission list.

    /**
     * @brief Checks preconditions for creating a service (payload present,
     * provider is owner/admin).
     * @tparam T Service type, constrained by Service_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_j Payload containing the initial
     * owner_id/admin_id.
     * @param[out] error Receives denial details.
     * @return True when the creation preconditions hold.
     */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    bool canCreate(const Requester&                     requester,
                   const std::optional<jsoncons::json>& service_j,
                   Http::Error& error);  // Synchronous create check;
                                         // the payload identifies
                                         // the target service.

    /** @brief Coroutine form of canCreate() for services. */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    drogon::Task<bool> canCreateAsync(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& service_j,
        Http::Error& error);  // Coroutine create check using
                              // the service payload.

    /**
     * @brief Allows service reads for the owner, admin, or staff with the
     * CAN_READ bit.
     * @tparam T Service type, constrained by Service_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_id Id of the service whose permissions are loaded.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    bool canRead(const Requester& requester,
                 uint64_t         service_id,
                 Http::Error&     error);  // Synchronous read check for a
                                           // service or appointment.

    /** @brief Coroutine form of canRead() for services. */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    drogon::Task<bool> canReadAsync(
        const Requester& requester,
        uint64_t         service_id,
        Http::Error&     error);  // Coroutine read check for a
                                  // service or appointment.

    /**
     * @brief Allows service updates for the owner, admin, or staff with the
     * CAN_WRITE bit.
     * @tparam T Service type, constrained by Service_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_id Id of the service whose permissions are loaded.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    bool canUpdate(const Requester& requester,
                   uint64_t         service_id,
                   Http::Error&     error);  // Synchronous update check for
                                             // a service or appointment.

    /** @brief Coroutine form of canUpdate() for services. */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    drogon::Task<bool> canUpdateAsync(
        const Requester& requester,
        uint64_t         service_id,
        Http::Error&     error);  // Coroutine update check for a
                                  // service or appointment.

    /**
     * @brief Allows service deletion for the owner only.
     * @tparam T Service type, constrained by Service_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_id Id of the service whose permissions are loaded.
     * @param[out] error Receives denial details.
     * @return True when the requester is the owner.
     */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    bool canDelete(const Requester& requester,
                   uint64_t         service_id,
                   Http::Error&     error);  // Synchronous delete check for
                                             // a service.

    /** @brief Coroutine form of canDelete() for services. */
    template <Service_t T>  // Restricts this overload to service types
                            // (clinic/pharmacy/lab/radiology).
    drogon::Task<bool> canDeleteAsync(
        const Requester& requester,
        uint64_t         service_id,
        Http::Error&     error);  // Coroutine delete check for a
                                  // service.

    /**
     * @brief Allows staff management for the service owner or admin only.
     * @tparam T Entity type exposing
     * getTableName()/getServicePermissionsQuery().
     * @param[in] requester Authenticated caller.
     * @param[in] service_id Id of the service whose permissions are loaded.
     * @param[out] error Receives denial details.
     * @return True when the requester is owner or admin.
     */
    template <typename T>  // Generic overload; T must provide the
                           // table/query helpers used inside.
    bool canManageStaff(const Requester& requester,
                        uint64_t         service_id,
                        Http::Error& error);  // Synchronous check; only
                                              // owner/admin may manage staff.

    /** @brief Coroutine form of canManageStaff(). */
    template <typename T>  // Generic overload; T must provide the
                           // table/query helpers used inside.
    drogon::Task<bool> canManageStaffAsync(
        const Requester& requester,
        uint64_t         service_id,
        Http::Error&     error);  // Coroutine form of the
                                  // staff-management check.

    // case is a data set inside clinic workflow: permission comes from the
    // parent service.

    /**
     * @brief Allows creating a case for owner/admin or staff with the
     * CAN_WRITE bit.
     * @details The permission query is derived from @p data_j (which
     * identifies the parent service) via T::getPermissionsQueryForCreate.
     * @tparam T Case type, constrained by Case_t.
     * @param[in] requester Authenticated caller.
     * @param[in] data_j Payload used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Case_t T>  // Restricts this overload to case data types.
    bool canCreate(const Requester&                     requester,
                   const std::optional<jsoncons::json>& data_j,
                   Http::Error& error);  // Synchronous create check; true when
                                         // the action is allowed.

    /** @brief Coroutine form of canCreate() for cases. */
    template <Case_t T>  // Restricts this overload to case data types.
    drogon::Task<bool> canCreateAsync(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& data_j,
        Http::Error& error);  // Coroutine create check; co_await it from an
                              // async route.

    /**
     * @brief Allows reading a case for owner/admin or staff with the
     * CAN_READ bit.
     * @tparam T Case type, constrained by Case_t.
     * @param[in] requester Authenticated caller.
     * @param[in] case_id Id used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Case_t T>  // Restricts this overload to case data types.
    bool canRead(const Requester& requester,
                 uint64_t         case_id,
                 Http::Error&     error);  // Synchronous read check for a case.

    /** @brief Coroutine form of canRead() for cases. */
    template <Case_t T>  // Restricts this overload to case data types.
    drogon::Task<bool> canReadAsync(
        const Requester& requester,
        uint64_t         case_id,
        Http::Error&     error);  // Coroutine read check for a case.

    /**
     * @brief Allows updating a case for owner/admin or staff with the
     * CAN_WRITE bit.
     * @tparam T Case type, constrained by Case_t.
     * @param[in] requester Authenticated caller.
     * @param[in] case_id Id used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Case_t T>  // Restricts this overload to case data types.
    bool canUpdate(const Requester& requester,
                   uint64_t         case_id,
                   Http::Error& error);  // Synchronous update check for a case.

    /** @brief Coroutine form of canUpdate() for cases. */
    template <Case_t T>  // Restricts this overload to case data types.
    drogon::Task<bool> canUpdateAsync(
        const Requester& requester,
        uint64_t         case_id,
        Http::Error&     error);  // Coroutine update check for a case.

    /**
     * @brief Allows deleting a case for owner/admin or staff with the
     * CAN_DELETE bit.
     * @tparam T Case type, constrained by Case_t.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Case_t T>  // Restricts this overload to case data types.
    bool canDelete(const Requester& requester,
                   uint64_t         _id,
                   Http::Error& error);  // Synchronous delete check; _id is the
                                         // target entity id.

    /** @brief Coroutine form of canDelete() for cases. */
    template <Case_t T>  // Restricts this overload to case data types.
    drogon::Task<bool> canDeleteAsync(
        const Requester& requester,
        uint64_t         _id,
        Http::Error&
            error);  // Coroutine delete check; _id is the target entity id.

    // appointment is either for any service: the target clinic id comes
    // from the request payload.

    /**
     * @brief Allows creating an appointment for owner/admin or staff with
     * the CAN_WRITE bit.
     * @details The target service id is extracted from @p service_j
     * ("clinic_id"); a missing or invalid value fails the check before any
     * permission lookup.
     * @tparam T Appointment type, constrained by Appointment_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_j Payload containing the clinic/service id.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    bool canCreate(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& service_j,
        Http::Error& error);  // Synchronous create check; the payload
                              // identifies the target service.

    /** @brief Coroutine form of canCreate() for appointments. */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    drogon::Task<bool> canCreateAsync(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& service_j,
        Http::Error&
            error);  // Coroutine create check using the service payload.

    /**
     * @brief Allows reading an appointment for owner/admin or staff with
     * the CAN_READ bit.
     * @tparam T Appointment type, constrained by Appointment_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_id Id used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    bool canRead(const Requester& requester,
                 uint64_t         service_id,
                 Http::Error& error);  // Synchronous read check for a service
                                       // or appointment.

    /** @brief Coroutine form of canRead() for appointments. */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    drogon::Task<bool> canReadAsync(
        const Requester& requester,
        uint64_t         service_id,
        Http::Error&
            error);  // Coroutine read check for a service or appointment.

    /**
     * @brief Allows updating an appointment for owner/admin or staff with
     * the CAN_WRITE bit.
     * @tparam T Appointment type, constrained by Appointment_t.
     * @param[in] requester Authenticated caller.
     * @param[in] service_id Id used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    bool canUpdate(const Requester& requester,
                   uint64_t         service_id,
                   Http::Error&     error);  // Synchronous update check for a
                                             // service or appointment.

    /** @brief Coroutine form of canUpdate() for appointments. */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    drogon::Task<bool> canUpdateAsync(
        const Requester& requester,
        uint64_t         service_id,
        Http::Error&
            error);  // Coroutine update check for a service or appointment.

    /**
     * @brief Allows deleting an appointment for owner/admin or staff with
     * the CAN_DELETE bit.
     * @tparam T Appointment type, constrained by Appointment_t.
     * @param[in] requester Authenticated caller.
     * @param[in] _id Id used to build the permission query.
     * @param[out] error Receives denial details.
     * @return True when the requester holds sufficient permission.
     */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    bool canDelete(const Requester& requester,
                   uint64_t         _id,
                   Http::Error& error);  // Synchronous delete check; _id is the
                                         // target entity id.

    /** @brief Coroutine form of canDelete() for appointments. */
    template <Appointment_t T>  // Restricts this overload to appointment types.
    drogon::Task<bool> canDeleteAsync(
        const Requester& requester,
        uint64_t         _id,
        Http::Error&
            error);  // Coroutine delete check; _id is the target entity id.

   private:  // No private state; keeping the section empty documents that
             // the class is stateless.
  };
}  // namespace api::v2
