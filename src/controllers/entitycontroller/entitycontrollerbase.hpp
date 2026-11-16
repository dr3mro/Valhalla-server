/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file entitycontrollerbase.hpp
 * @brief Abstract contract for the request-facing CRUDS handlers exposed for
 * every entity type.
 * @details This header declares `EntityControllerBase`, a pure interface: it
 * lists the ten operations (Create/Read/ Update/Delete/Search and their Async
 * twins) that every ordinary entity handler must provide. It contains no
 * implementation at all - each method ends in `= 0`, meaning "derived classes
 * must fill this in".
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It is the contract between the HTTP routing layer and the concrete
 * EntityController<T>. Drogon handlers program against this base type, so they
 * can treat a User, a Patient or a Report uniformly. Without this interface the
 * router would have to know every entity type, and there would be no single
 * place guaranteeing that all entities expose the same CRUDS operations. The
 * real work is done by EntityController (see entitycontroller.hpp), which
 * implements everything declared here.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Virtual functions / polymorphism:
 * https://en.cppreference.com/w/cpp/language/virtual
 * - Abstract classes and pure virtual functions (`= 0`):
 * https://en.cppreference.com/w/cpp/language/abstract_class
 * - Callback / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 * - Coroutines (co_await / drogon::Task):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - std::optional and std::string_view:
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once

#include <cstdint>  // For uint64_t, the type used for entity ids.
// Ids are optional: a missing id is a valid (rejectable) input.
#include <optional>
#include <string_view>  // A non-owning view over the request body string.

// Drogon Task type used by the Async variants.
#include "drogon/utils/coroutine.h"
// Defines CALLBACK_, the reply function type.
#include "utils/global/callback.hpp"
// The authenticated principal passed to every operation.
#include "utils/global/requester.hpp"

// The identity/authorization principal passed by the HTTP layer to every
// operation.
using Requester =
    api::v2::Requester;  // Short name for the identity/authorization principal.

/**
 * @class EntityControllerBase
 * @brief Abstract contract for the request-facing CRUDS handlers exposed for
 * every entity type.
 * @details Declares five operations (Create, Read, Update, Delete, Search),
 * each in a synchronous callback form and in a `drogon::Task` coroutine form.
 * Implementations (see EntityController) are expected to parse and validate @p
 * data, authorize the action with @p requester, construct the entity and
 * delegate persistence to the lower-level Controller. The callback is
 * `CALLBACK_` = `std::function<void(int, const std::string&)>`: implementations
 * invoke it once with an HTTP status code and a response body, including on
 * error paths.
 *
 *          The interface itself does not own or store any state; derived
 * controllers supply the collaborators. The lifetime of @p callback, @p
 * requester and @p data (and of the owning controller) must extend until the
 *          coroutine variants complete, because they are borrowed by the
 * coroutine frame.
 * @note The `const Requester&&` parameter is passed by rvalue reference by the
 * existing callers; treat it as a borrowed, immutable principal rather than
 * assuming ownership is transferred.
 * @see EntityController, Controller
 */
class EntityControllerBase
{
 public:
  explicit EntityControllerBase() =
      default;  // Nothing to initialize; the interface holds no state.
  EntityControllerBase(const EntityControllerBase&) =
      default;  // Copyable because there is no state to duplicate.
  EntityControllerBase(EntityControllerBase&&) =
      default;  // Movable for the same reason.
  EntityControllerBase& operator=(const EntityControllerBase&) =
      default;  // Assignment is a no-op copy of no state.
  EntityControllerBase& operator=(EntityControllerBase&&) =
      default;  // Move assignment is likewise a no-op.

  /**
   * @brief Virtual destructor so derived controllers can be destroyed through
   * this interface.
   */
  virtual ~EntityControllerBase() =
      default;  // Virtual so deleting a derived object through this base is
                // safe.

  // CRUDS
  /**
   * @brief Creates an entity from the JSON request body.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal used to authorize the create.
   * @param[in] data JSON request body; must be parseable by the entity's
   * create schema.
   */
  virtual void Create(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::string_view data) = 0;  // Pure virtual: every entity
                                                   // must implement creation.
  /**
   * @brief Coroutine form of Create(); must be awaited.
   * @details Borrows all arguments, so they must remain valid until the
   * coroutine completes.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal used to authorize the create.
   * @param[in] data JSON request body.
   */
  virtual drogon::Task<void> CreateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine form of Create.
  /**
   * @brief Reads an entity identified in the JSON request body (id plus
   * requested column set).
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON request body containing `id` and `schema`.
   */
  virtual void Read(CALLBACK_&&       callback,
                    const Requester&& requester,
                    std::string_view  data) = 0;  // Pure virtual: every entity
                                                  // must implement reading.
  /**
   * @brief Coroutine form of Read(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal used to authorize the read.
   * @param[in] data JSON request body containing `id` and `schema`.
   */
  virtual drogon::Task<void> ReadAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine form of Read.
  /**
   * @brief Updates the entity identified by @p _id using fields from @p data.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON request body holding the fields to change.
   * @param[in] _id Target entity id; a missing value is rejected by
   * implementations (400).
   */
  virtual void Update(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::string_view  data,
                      std::optional<uint64_t>
                          _id) = 0;  // Pure virtual: update needs a target id.
  /**
   * @brief Coroutine form of Update(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal used to authorize the update.
   * @param[in] data JSON request body holding the fields to change.
   * @param[in] _id Target entity id; a missing value is rejected by
   * implementations (400).
   */
  virtual drogon::Task<void> UpdateAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data,
      std::optional<uint64_t>
          _id) = 0;  // Pure virtual coroutine form of Update.
  /**
   * @brief Deletes the entity identified by @p _id.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] _id Target entity id; a missing value is rejected by
   * implementations (406).
   */
  virtual void Delete(CALLBACK_&&             callback,
                      const Requester&&       requester,
                      std::optional<uint64_t> _id) = 0;  // Pure virtual: delete
                                                         // acts on a single id.
  /**
   * @brief Coroutine form of Delete(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal used to authorize the deletion.
   * @param[in] _id Target entity id; a missing value is rejected by
   * implementations (406).
   */
  virtual drogon::Task<void> DeleteAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::optional<uint64_t>
          _id) = 0;  // Pure virtual coroutine form of Delete.
  /**
   * @brief Searches entities according to criteria in @p data and returns a
   * paginated JSON envelope.
   * @param[in,out] callback Invoked exactly once with the resulting HTTP
   * status and body.
   * @param[in] requester Principal supplied by the caller; the generic
   * implementation does not consult it.
   * @param[in] data JSON request body containing search criteria, limit and
   * offset.
   */
  virtual void Search(CALLBACK_&&       callback,
                      const Requester&& requester,
                      std::string_view  data) = 0;  // Pure virtual: paginated
                                                    // search over criteria.
  /**
   * @brief Coroutine form of Search(); must be awaited.
   * @param[in,out] callback Invoked exactly once on completion.
   * @param[in] requester Principal supplied by the caller; the generic
   * implementation does not consult it.
   * @param[in] data JSON request body containing search criteria, limit and
   * offset.
   */
  virtual drogon::Task<void> SearchAsync(
      CALLBACK_&&       callback,
      const Requester&& requester,
      std::string_view  data) = 0;  // Pure virtual coroutine form of Search.
};
