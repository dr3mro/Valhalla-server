/**
 * @file cliniccontrollerbase.hpp
 * @brief Abstract contract for clinic-service entity handlers (patients,
 * visits, reports, prescriptions, ...).
 * @details Declares `ClinicControllerBase`, a pure interface for clinical data.
 * It keeps the subset of CRUDS that makes sense for medical records and adds
 * GetVisits for a patient's visit history. As with the other bases, every
 * operation is pure virtual (`= 0`) and answers through a CALLBACK_.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Clinical entities do not all support the same actions: for example, only
 * Patient and Visits can be deleted, and only Patient supports search and visit
 * lookup. This interface defines the common surface the HTTP layer can rely on,
 * while letting the concrete ClinicController return a clear "not implemented"
 * for unsupported actions. Without it, clinic endpoints would have no uniform
 * contract.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions / abstract classes:
 * https://en.cppreference.com/w/cpp/language/abstract_class
 * - Callback / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::optional (an id that may be missing):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Namespaces and `using namespace`:
 * https://en.cppreference.com/w/cpp/language/namespace
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

// Drogon helper for inspecting callable signatures.
#include <drogon/utils/FunctionTraits.h>

#include "drogon/utils/coroutine.h"  // Drogon Task type for the Async variants.
// Defines CALLBACK_, the reply function type.
#include "utils/global/callback.hpp"
#include "utils/global/requester.hpp"  // The authenticated principal type.

using namespace api::v2;  // Bring project types (Requester, Http, ...) into
                          // this file's scope.

/**
 * @class ClinicControllerBase
 * @brief Abstract contract for clinic-service entity handlers (patients,
 * visits, reports, prescriptions, ...).
 * @details Defines the subset of CRUDS operations meaningful for clinic
 * entities plus patient visit retrieval. Implementations (ClinicController)
 * specialize behaviour per entity type: for example only Patient/Visits support
 * Delete, only Patient supports Search and GetVisits. `CALLBACK_` is
 *          `std::function<void(int, const std::string&)>`; implementations
 * answer exactly once with an HTTP status code and body on every completing
 * path, including "not implemented" rejections.
 * @note Only Create, Read, Delete and GetVisits have coroutine forms in this
 * contract; Update and Search are synchronous only.
 * @see ClinicController, EntityControllerBase
 */
class ClinicControllerBase
{
 public:
  explicit ClinicControllerBase() =
      default;  // No state to initialize in an interface.

  virtual ~ClinicControllerBase() =
      default;  // Virtual so deletion through this base is safe.

  // CRUDS
  /**
   * @brief Creates a clinic-service entity from the JSON body.
   * @details For most types the id is supplied by the client; for
   * Patient/Visits the generic create flow is used.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body describing the new entity.
   */
  virtual void Create(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual: create a clinic entity.
  /**
   * @brief Coroutine form of Create(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize creation.
   * @param[in] data JSON body; must outlive the coroutine.
   */
  virtual drogon::Task<void> CreateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine create.
  /**
   * @brief Reads selected columns of a clinic-service entity identified in @p
   * data.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON body containing `id` and `schema`.
   */
  virtual void Read(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual: read selected columns.
  /**
   * @brief Coroutine form of Read(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON body containing `id` and `schema`; must outlive the
   * coroutine.
   */
  virtual drogon::Task<void> ReadAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine read.
  /**
   * @brief Updates a clinic-service entity identified by @p id.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON body holding the fields to change.
   * @param[in] id Target entity id.
   */
  virtual void Update(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::string_view        data,
      std::optional<uint64_t> id) = 0;  // Pure virtual: update an entity by id.
  /**
   * @brief Deletes a clinic-service entity; only supported for Patient/Visits
   * by the implementation.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body (400 when unsupported).
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] id Target entity id.
   */
  virtual void Delete(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          id) = 0;  // Pure virtual: delete (many clinic types reject this).
  /**
   * @brief Coroutine form of Delete(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] id Target entity id.
   */
  virtual drogon::Task<void> DeleteAsync(
      CALLBACK_&&             callback,
      const Requester&&       requester,
      std::optional<uint64_t> id) = 0;  // Pure virtual coroutine delete.
  /**
   * @brief Searches clinic-service entities; only supported for Patient by
   * the implementation.
   * @param[in,out] callback Invoked once with the resulting HTTP status and
   * body (400 when unsupported).
   * @param[in] requester Principal supplied by the caller.
   * @param[in] data JSON body containing criteria, limit and offset.
   */
  virtual void Search(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::string_view data) = 0;  // Pure virtual: search (only
                                                   // Patient supports it).
  /**
   * @brief Retrieves the visit history of a patient; unsupported for other
   * entity types.
   * @param[in,out] callback Invoked once with 200 and the serialized visits,
   * or an error status.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] patient_id Target patient id; a missing value is rejected with
   * 400.
   */
  virtual void GetVisits(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          patient_id) = 0;  // Pure virtual: fetch a patient's visits.
  /**
   * @brief Coroutine form of GetVisits(); must be awaited.
   * @param[in,out] callback Invoked once on completion.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] patient_id Target patient id; a missing value is rejected with
   * 400.
   */
  virtual drogon::Task<void> GetVisitsAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          patient_id) = 0;  // Pure virtual coroutine visit lookup.
};
