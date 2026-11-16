/**
 * @file entitycontroller.hpp
 * @brief Generic request-facing CRUDS handler: validates and authorizes a
 * request, then delegates persistence to Controller.
 * @details Declares `EntityController<T>`, the class that turns an HTTP request
 * body into a validated, authorized entity and then hands it to the lower-level
 * Controller for persistence. For each of the ten operations it follows the
 * same recipe: parse JSON, validate against the entity schema, ask GateKeeper
 * for permission, build the entity, and call the matching Controller method.
 * The .cpp file holds the definitions.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the bridge between "a request came in" and "a row was written". The
 * generic ClientController and ClinicController both inherit from it, reusing
 * its Read/Search/Update flows. Without this class each entity type would need
 * its own copy of the parse-validate-authorize-delegate pipeline, and
 * validation/security rules would drift.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Templates: https://en.cppreference.com/w/cpp/language/templates
 * - Multiple inheritance (this class inherits two bases):
 * https://en.cppreference.com/w/cpp/language/derived_class
 * - `override` and virtual functions:
 * https://en.cppreference.com/w/cpp/language/virtual
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::shared_ptr (sharing the GateKeeper service):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - Callback / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once
#include <fmt/format.h>  // Formatting error messages with {} placeholders.

#include <cstdint>  // For uint64_t entity ids.
// The JSON value type used to parse request bodies.
#include <jsoncons/json.hpp>
#include <memory>       // std::shared_ptr for the shared GateKeeper.
#include <optional>     // Optional ids passed to Update/Delete.
#include <string_view>  // Non-owning view of the request body.

// The persistence engine this class delegates to.
#include "controllers/base/controller/controller.hpp"
// The interface this class implements.
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "drogon/utils/coroutine.h"  // Drogon Task type for the Async variants.
#include "gatekeeper/gatekeeper.hpp"  // Authorization service declaration.
#include "store/store.hpp"  // Service locator for the shared GateKeeper.
// Defines CALLBACK_, the reply function type.
#include "utils/global/callback.hpp"

using GateKeeper =
    api::v2::GateKeeper;  // Short name for the authorization service.

/**
 * @class EntityController
 * @brief Generic request-facing CRUDS handler: validates and authorizes a
 * request, then delegates persistence to Controller.
 * @details Combines the persistence engine (Controller) with the abstract CRUDS
 * interface (EntityControllerBase). Each operation follows the same pipeline:
 * parse/validate the JSON body against the entity's database schema, ask
 * GateKeeper whether @p requester may perform the action, construct the entity
 * from the appropriate Types data carrier, and call the matching Controller
 * method (or its coroutine counterpart). Failure at any stage answers through
 * @p callback and returns early, so no database connection is taken for a
 * request that fails validation or authorization.
 *
 * @tparam T Entity type whose static `getTableName()` and data-carrier
 * constructors (Create_t/Read_t/Update_t/ Delete_t/Search_t) define the schema
 * used for validation and persistence.
 * @note All operations are `override` implementations of EntityControllerBase.
 * Coroutine variants borrow their arguments and must be awaited while those
 * arguments (and the controller) remain alive.
 * @see EntityControllerBase, Controller, GateKeeper
 */
template <typename T>
class EntityController : public Controller, public EntityControllerBase {
 public:
  EntityController() = default;  // Uses the base GateKeeper initializer.
  EntityController(const EntityController&) =
      default;  // Copying shares the GateKeeper handle.
  EntityController(EntityController&&) =
      default;  // Moving transfers the GateKeeper handle.
  EntityController& operator=(const EntityController&) =
      default;  // Copy assignment shares the handle.
  EntityController& operator=(EntityController&&) =
      default;  // Move assignment transfers the handle.
  ~EntityController() override =
      default;  // Virtual via both bases; no explicit cleanup needed.
  // CRUDS

  /**
   * @brief Validates and authorizes a create request, then inserts the new
   * entity.
   * @details Ignores server-controlled `id` in the schema rule; the database
   * assigns the primary key.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal whose create permission is checked by
   * GateKeeper.
   * @param[in] data JSON body describing the new entity.
   */
  void Create(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data) override;
  /**
   * @brief Coroutine form of Create(); awaits the asynchronous gate and
   * persistence checks.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal whose create permission is checked.
   * @param[in] data JSON body describing the new entity; must outlive the
   * coroutine.
   */
  drogon::Task<void> CreateAsync(CALLBACK_&&       callback,
                                 const Requester&& requester,
                                 std::string_view  data) override;
  /**
   * @brief Validates and authorizes a read request, then reads the selected
   * columns of an entity.
   * @details The body must carry a numeric `id` and a `schema` array;
   * forbidden fields (e.g. `password`) are rejected by the read-schema rule.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal whose read permission is checked.
   * @param[in] data JSON body containing `id` and `schema`.
   */
  void Read(CALLBACK_&&       callback,
            const Requester&& requester,
            std::string_view  data) override;
  /**
   * @brief Coroutine form of Read(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal whose read permission is checked.
   * @param[in] data JSON body containing `id` and `schema`; must outlive the
   * coroutine.
   */
  drogon::Task<void> ReadAsync(CALLBACK_&&       callback,
                               const Requester&& requester,
                               std::string_view  data) override;
  /**
   * @brief Validates and authorizes an update request, then updates the
   * entity.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal whose update permission is checked.
   * @param[in] data JSON body holding the fields to change.
   * @param[in] _id Target entity id; a missing value is rejected with 400.
   */
  void Update(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::string_view        data,
              std::optional<uint64_t> _id) override;
  /**
   * @brief Coroutine form of Update(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal whose update permission is checked.
   * @param[in] data JSON body holding the fields to change; must outlive the
   * coroutine.
   * @param[in] _id Target entity id; a missing value is rejected with 400.
   */
  drogon::Task<void> UpdateAsync(CALLBACK_&&             callback,
                                 const Requester&&       requester,
                                 std::string_view        data,
                                 std::optional<uint64_t> _id) override;
  /**
   * @brief Authorizes and performs a delete; a missing id is rejected with
   * 406.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal whose delete permission is checked.
   * @param[in] _id Target entity id.
   */
  void Delete(CALLBACK_&&             callback,
              const Requester&&       requester,
              std::optional<uint64_t> _id) override;
  /**
   * @brief Coroutine form of Delete(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal whose delete permission is checked.
   * @param[in] _id Target entity id.
   */
  drogon::Task<void> DeleteAsync(CALLBACK_&&             callback,
                                 const Requester&&       requester,
                                 std::optional<uint64_t> _id) override;
  /**
   * @brief Parses search criteria into a Search_t and runs the paginated
   * search.
   * @note The requester is accepted for interface uniformity but is not used
   * by this implementation.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Unused principal.
   * @param[in] data JSON body containing criteria, limit and offset.
   */
  void Search(CALLBACK_&&       callback,
              const Requester&& requester,
              std::string_view  data) override;
  /**
   * @brief Coroutine form of Search(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Unused principal.
   * @param[in] data JSON body containing criteria, limit and offset; must
   * outlive the coroutine.
   */
  drogon::Task<void> SearchAsync(CALLBACK_&&       callback,
                                 const Requester&& requester,
                                 std::string_view  data) override;

 private:
  /**
   * Shared GateKeeper used by every operation to authorize actions on behalf
   * of the requester.
   */
  std::shared_ptr<GateKeeper> gateKeeper =
      Store::getObject<GateKeeper>();  // Shared authorization service used by
                                       // every operation.
};
