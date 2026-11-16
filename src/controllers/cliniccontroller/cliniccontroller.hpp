/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file cliniccontroller.hpp
 * @brief Clinic-entity handler that specializes generic CRUDS behaviour per
 * entity type via constrained helpers.
 * @details Declares `ClinicController<T>`, which inherits storage from
 * EntityController<T> and the clinic contract from ClinicControllerBase. Public
 * methods are thin forwarders to private `...Impl` helpers; each helper has a
 * C++20 `requires` clause so the compiler picks the right behaviour for the
 * concrete entity type. For example, Patient/Visits use server-assigned ids and
 * support Delete, while other types carry a client id and get a "not
 * implemented" answer for Delete/Search/GetVisits.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Clinic data has type-specific rules that a single generic handler cannot
 * express cleanly. This class centralizes those rules using compile-time
 * constraints, so the public API stays uniform while the correct behaviour is
 * chosen automatically for each entity. Without it, every clinic handler would
 * need hand-written special cases.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Requires clauses / constraints (C++20):
 * https://en.cppreference.com/w/cpp/language/requires
 * - Templates: https://en.cppreference.com/w/cpp/language/templates
 * - `final` and virtual overrides:
 * https://en.cppreference.com/w/cpp/language/virtual
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::shared_ptr (sharing the GateKeeper):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

#include <cstdint>  // For uint64_t entity ids.

// The interface implemented here.
#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
// Base that supplies generic CRUDS.
#include "controllers/entitycontroller/entitycontroller.hpp"
// Patient type used in the constraints.
#include "entities/services/clinics/patient/patient.hpp"
// Visits type used in the constraints.
#include "entities/services/clinics/visits/visits.hpp"
#include "utils/global/global.hpp"  // Project-wide shared helpers.

/**
 * @class ClinicController
 * @brief Clinic-entity handler that specializes generic CRUDS behaviour per
 * entity type via constrained helpers.
 * @details Inherits persistence from EntityController<T> and the clinic
 * contract from ClinicControllerBase. Public methods are thin forwarders to
 * private `...Impl` overloads selected by C++20 `requires` clauses. This lets
 * the controller distinguish, for example, Patient/Visits (which use
 * server-assigned ids and support Delete) from other clinic entities (which
 * carry a client-supplied id and reject Delete/Search/GetVisits).
 * @tparam T Clinic entity type; the constraints inside each helper decide which
 * specialization applies.
 * @note All public overrides are `final`. Coroutine variants borrow their
 * arguments and must be awaited while alive.
 * @see ClinicControllerBase, EntityController
 */
template <typename T>
class ClinicController : public EntityController<T>, public ClinicControllerBase
{
 private:
  /**
   * Shared authorization service used by the specialized create/delete/visit
   * paths.
   */
  std::shared_ptr<GateKeeper> gateKeeper =
      Store::getObject<GateKeeper>();  // Shared authorization service for the
                                       // specialized paths.

  /**
   * @brief Creates an entity whose id is supplied by the client (all types
   * except Patient/Visits).
   * @details Validates the create schema but explicitly checks that `id` is
   * present and fits 64 bits, since the validator was told to ignore it, then
   * authorizes and delegates to Controller::Create().
   * @tparam U Deduced entity type; this overload is selected when U is
   * neither Patient nor Visits.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body containing the entity fields and an explicit
   * `id`.
   */
  template <typename U = T>
  void CreateImpl(CALLBACK_&&       callback,
                  const Requester&& requester,
                  std::string_view  data)
    requires(!std::is_same<U, Patient>::value &&
             !std::is_same<U, Visits>::value);

  /**
   * @brief Patient/Visits create specialization: uses the generic
   * EntityController::Create() flow.
   * @tparam U Deduced entity type; selected when U is Patient or Visits.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body describing the new entity.
   */
  template <typename U = T>
  void CreateImpl(CALLBACK_&&       callback,
                  const Requester&& requester,
                  std::string_view  data)
    requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value);

  /**
   * @brief Coroutine form of the client-supplied-id create path; awaits the
   * gate and the write executor.
   * @tparam U Deduced entity type; selected when U is neither Patient nor
   * Visits.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body containing the entity fields and an explicit
   * `id`.
   */
  template <typename U = T>
  drogon::Task<void> CreateAsyncImpl(CALLBACK_&&       callback,
                                     const Requester&& requester,
                                     std::string_view  data)
    requires(!std::is_same<U, Patient>::value &&
             !std::is_same<U, Visits>::value);

  /**
   * @brief Coroutine form of the Patient/Visits create specialization.
   * @tparam U Deduced entity type; selected when U is Patient or Visits; must
   * outlive the coroutine.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body describing the new entity.
   */
  template <typename U = T>
  drogon::Task<void> CreateAsyncImpl(CALLBACK_&&       callback,
                                     const Requester&& requester,
                                     std::string_view  data)
    requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value);

  /**
   * @brief Patient/Visits delete specialization: delegates to
   * EntityController::Delete().
   * @tparam U Deduced entity type; selected when U is Patient or Visits.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize deletion.
   * @param[in] id Target entity id.
   */
  template <typename U = T>
  void DeleteImpl(CALLBACK_&&             callback,
                  const Requester&&       requester,
                  std::optional<uint64_t> id)
    requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value);

  /**
   * @brief Delete specialization for types where deletion is not implemented:
   * always answers 400.
   * @tparam U Deduced entity type; selected when U is neither Patient nor
   * Visits.
   * @param[in,out] callback Invoked once with a 400 "not implemented"
   * response; never performs a DB operation.
   * @param[in] requester Unused principal.
   * @param[in] id Unused target id.
   */
  template <typename U = T>
  void DeleteImpl(CALLBACK_&&                        callback,
                  [[maybe_unused]] const Requester&& requester,
                  std::optional<uint64_t>            id)
    requires(!std::is_same<U, Patient>::value &&
             !std::is_same<U, Visits>::value);

  /**
   * @brief Coroutine form of the Patient/Visits delete specialization.
   * @tparam U Deduced entity type; selected when U is Patient or Visits.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize deletion.
   * @param[in] id Target entity id.
   */
  template <typename U = T>
  drogon::Task<void> DeleteAsyncImpl(CALLBACK_&&             callback,
                                     const Requester&&       requester,
                                     std::optional<uint64_t> id)
    requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value);

  /**
   * @brief Coroutine form of the unsupported-delete path: answers 400 without
   * DB work.
   * @tparam U Deduced entity type; selected when U is neither Patient nor
   * Visits.
   * @param[in,out] callback Invoked once with a 400 "not implemented"
   * response.
   * @param[in] requester Unused principal.
   * @param[in] id Unused target id.
   */
  template <typename U = T>
  drogon::Task<void> DeleteAsyncImpl(
      CALLBACK_&&                        callback,
      [[maybe_unused]] const Requester&& requester,
      std::optional<uint64_t>            id)
    requires(!std::is_same<U, Patient>::value &&
             !std::is_same<U, Visits>::value);

  /**
   * @brief Search specialization for non-Patient types: always answers 400
   * (not implemented).
   * @tparam U Deduced entity type; selected when U is not Patient.
   * @param[in,out] callback Invoked once with a 400 "not implemented"
   * response.
   * @param[in] requester Unused principal.
   * @param[in] data Unused search criteria.
   */
  template <typename U = T>
  void SearchImpl(CALLBACK_&&       callback,
                  const Requester&& requester,
                  std::string_view  data)
    requires(!std::is_same<U, Patient>::value);

  /**
   * @brief Patient search specialization: delegates to
   * EntityController::Search().
   * @tparam U Deduced entity type; selected when U is Patient.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing criteria, limit and offset.
   */
  template <typename U = T>
  void SearchImpl(CALLBACK_&&       callback,
                  const Requester&& requester,
                  std::string_view  data)
    requires(std::is_same<U, Patient>::value);

  /**
   * @brief Patient specialization: authorizes and runs
   * Controller::GetVisits().
   * @tparam U Deduced entity type; selected when U is Patient.
   * @param[in,out] callback Invoked once with 200 and the serialized visits,
   * or an error status.
   * @param[in] requester Principal whose read permission is checked.
   * @param[in] patient_id Target patient id; a missing value is rejected with
   * 400.
   */
  template <typename U = T>
  void GetVisitsImpl(CALLBACK_&&             callback,
                     const Requester&&       requester,
                     std::optional<uint64_t> patient_id)
    requires(std::is_same<U, Patient>::value);

  /**
   * @brief Visit-lookup specialization for non-Patient types: answers 400
   * (not implemented).
   * @tparam U Deduced entity type; selected when U is not Patient.
   * @param[in,out] callback Invoked once with a 400 "not implemented"
   * response.
   * @param[in] requester Unused principal.
   * @param[in] patient_id Unused patient id.
   */
  template <typename U = T>
  void GetVisitsImpl(CALLBACK_&&             callback,
                     const Requester&&       requester,
                     std::optional<uint64_t> patient_id)
    requires(!std::is_same<U, Patient>::value);

  /**
   * @brief Coroutine form of the Patient visit lookup; awaits the gate and
   * Controller::GetVisitsAsync().
   * @tparam U Deduced entity type; selected when U is Patient.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal whose read permission is checked.
   * @param[in] patient_id Target patient id; a missing value is rejected with
   * 400.
   */
  template <typename U = T>
  drogon::Task<void> GetVisitsAsyncImpl(CALLBACK_&&             callback,
                                        const Requester&&       requester,
                                        std::optional<uint64_t> patient_id)
    requires(std::is_same<U, Patient>::value);

  /**
   * @brief Coroutine form of the unsupported visit lookup: answers 400
   * without DB work.
   * @tparam U Deduced entity type; selected when U is not Patient.
   * @param[in,out] callback Invoked once with a 400 "not implemented"
   * response.
   * @param[in] requester Unused principal.
   * @param[in] patient_id Unused patient id.
   */
  template <typename U = T>
  drogon::Task<void> GetVisitsAsyncImpl(
      CALLBACK_&&                              callback,
      [[maybe_unused]] const Requester&&       requester,
      [[maybe_unused]] std::optional<uint64_t> patient_id)
    requires(!std::is_same<U, Patient>::value);

 public:
  explicit ClinicController() =
      default;  // GateKeeper is initialized by its member initializer.
  ~ClinicController() override =
      default;  // Virtual destructor; no explicit cleanup needed.

  /** @brief Creates a clinic entity; forwards to the type-selected
   * CreateImpl(). */
  void Create(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data)
      final;  // Forwards to the type-selected CreateImpl.
  /** @brief Coroutine form of Create(); forwards to CreateAsyncImpl(). */
  drogon::Task<void> CreateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) final;  // Coroutine create forwarder.
  /** @brief Reads a clinic entity; delegates to EntityController::Read(). */
  void Read(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) final;  // Delegates to EntityController::Read.
  /** @brief Coroutine form of Read(); delegates to
   * EntityController::ReadAsync(). */
  drogon::Task<void> ReadAsync(CALLBACK_&&       callback,
                               const Requester&& requester,
                               std::string_view  data)
      final;  // Delegates to EntityController::ReadAsync.
  /** @brief Updates a clinic entity; delegates to EntityController::Update().
   */
  void Update(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::string_view        data,
              std::optional<uint64_t> id)
      final;  // Delegates to EntityController::Update.
  /** @brief Coroutine form of Update(); delegates to
   * EntityController::UpdateAsync(). */
  drogon::Task<void> UpdateAsync(CALLBACK_&&             callback,
                                 const Requester&&       requester,
                                 std::string_view        data,
                                 std::optional<uint64_t> id)
      final;  // Delegates to EntityController::UpdateAsync.
  /** @brief Deletes a clinic entity; forwards to the type-selected
   * DeleteImpl(). */
  void Delete(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::optional<uint64_t> id)
      final;  // Forwards to the type-selected DeleteImpl.
  /** @brief Coroutine form of Delete(); forwards to DeleteAsyncImpl(). */
  drogon::Task<void> DeleteAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> id) final;  // Coroutine delete forwarder.
  /** @brief Searches clinic entities; forwards to the type-selected
   * SearchImpl(). */
  void Search(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data)
      final;  // Forwards to the type-selected SearchImpl.
  /** @brief Retrieves a patient's visits; forwards to the type-selected
   * GetVisitsImpl(). */
  void GetVisits(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> patient_id) final;  // Forwards to GetVisitsImpl.
  /** @brief Coroutine form of GetVisits(); forwards to GetVisitsAsyncImpl().
   */
  drogon::Task<void> GetVisitsAsync(CALLBACK_&&             callback,
                                    const Requester&&       requester,
                                    std::optional<uint64_t> patient_id)
      final;  // Coroutine visit-lookup forwarder.
};
