/**
 * @file permissionmanager_private.hpp
 * @brief The behind-the-scenes helper functions used by PermissionManager.
 *        (Originally: "Implementation helpers shared by the PermissionManager
 * authorization overloads.")
 * @details This header declares static utilities that interpret the permission
 *          JSON (owner id, admin id and staff entries), compare the requester's
 *          identity, validate creation payloads, and fetch/cache permissions.
 *          The class is just a namespace-like container; it holds no data.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * PermissionManager delegates the real work here, so the same rules are applied
 * consistently to services, cases and appointments. Without these helpers the
 * public manager would have to repeat database lookups and JSON parsing in
 * every overload.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - JSON (JavaScript Object Notation): https://www.json.org/json-en.html
 * - Static class members: https://en.cppreference.com/w/cpp/language/static
 * - Service/caching layer: https://en.wikipedia.org/wiki/Cache_(computing)
 * - std::invoke (call any callable):
 * https://en.cppreference.com/w/cpp/utility/functional/invoke
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <cstdint>    // uint64_t for database ids.
#include <exception>  // std::exception, caught when JSON conversion fails.
// jsoncons::json, the permission documents we interpret.
#include <jsoncons/basic_json.hpp>
#include <memory>    // std::shared_ptr for the shared DatabaseController.
#include <optional>  // std::optional for values that may be absent.
#include <string>    // std::string for names, keys and messages.

// getPermissions()/getPermissionsCoro() read from PostgreSQL.
#include "controllers/databasecontroller/databasecontroller.hpp"
// The async database client used by the coroutine path.
#include "database/asyncdbclient.hpp"
#include "drogon/utils/coroutine.h"  // drogon::Task for the coroutine variants.
// PermissionCache, the cache used on lookups.
#include "gatekeeper/permissionmanager/permissioncache.hpp"
// PowerLevel and StaffPermission bit masks.
#include "gatekeeper/permissionmanager/permissions.hpp"
// Store, used to fetch the shared database controller.
#include "store/store.hpp"
#include "utils/global/http.hpp"  // Http::Error and status codes for denials.
// Requester, the caller whose identity is checked.
#include "utils/global/requester.hpp"
#include "utils/message/message.hpp"  // Logging helpers for errors.
namespace api::v2                     // Same namespace as the public manager.
{
  /**
   * @class PermissionManagerPrivate
   * @brief Implementation helpers shared by the PermissionManager
   * authorization overloads.
   * @details Pure static utilities: they interpret the permission JSON
   * returned by the database (owner/admin ids, staff capability strings),
   * enforce group/id checks and create preconditions, and fetch/cache entity
   * permissions through PermissionCache. Sync helpers use
   * DatabaseController::getPermissions; the
   *          `*Coro` variant awaits getPermissionsCoro. All denial/error
   * details are written to the caller-provided Http::Error.
   * @note The class holds no state; all members are static.
   *       In everyday words: this is a toolbox, not a machine. You never make
   *       one; you call its named tools with the facts and get a yes/no back.
   */
  class PermissionManagerPrivate  // A stateless collection of helper
                                  // functions.
  {
   public:
    PermissionManagerPrivate() = default;  // No state to initialize, so the
                                           // default constructor is enough.
    virtual ~PermissionManagerPrivate() =
        default;  // Virtual destructor for safe cleanup if used as a base.
    PermissionManagerPrivate(const PermissionManagerPrivate& other) =
        default;  // Copying a stateless helper is harmless.
    PermissionManagerPrivate& operator=(const PermissionManagerPrivate& other) =
        default;  // Copy assignment is harmless for the same reason.
    PermissionManagerPrivate(PermissionManagerPrivate&& other) noexcept =
        default;  // Moving is harmless and marked noexcept (cannot throw).
    PermissionManagerPrivate& operator=(
        PermissionManagerPrivate&& other) noexcept =
        default;  // Move assignment is harmless and cannot throw.

    /**
     * @brief Checks that the requester is a staff member of the service
     * holding @p powerlevel.
     * @param[in] requester Authenticated caller.
     * @param[in] permissions_j Service permission JSON (may be empty).
     * @param[in] powerlevel Capability bit required.
     * @param[in] service_name Service name used in messages.
     * @param[out] error Receives FORBIDDEN when the requester is not staff.
     * @return True when the staff entry's mask contains @p powerlevel.
     */
    static bool hasPermission(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& permissions_j,
        const Permissions::PowerLevel&
            powerlevel,  // Requires staff membership plus this capability
                         // bit; returns false otherwise.
        const std::string& service_name,
        Http::Error&       error);

    /**
     * @brief Checks that the requester is the service owner, comparing
     * against `owner_id`.
     * @param[in] requester Caller whose id is compared with `owner_id`.
     * @param[in] permissions_j Service permissions document.
     * @param[in] service_name Service/table name used in messages.
     * @param[out] error Receives BAD_REQUEST on missing permissions or
     * FORBIDDEN on mismatch.
     * @return True only when the ids match.
     */
    static bool isOwnerOfService(  // Compares the requester id with the
                                   // stored owner_id.
        const Requester&                     requester,
        const std::optional<jsoncons::json>& permissions_j,
        const std::string&                   service_name,
        Http::Error&                         error);

    /**
     * @brief Checks that the requester is the service admin, comparing
     * against `admin_id`.
     * @param[in] requester Caller whose id is compared with `admin_id`.
     * @param[in] permissions_j Service permissions document.
     * @param[in] service_name Service/table name used in messages.
     * @param[out] error Receives BAD_REQUEST on missing permissions or
     * FORBIDDEN on mismatch.
     * @return True only when the ids match.
     */
    static bool isAdminOfService(  // Compares the requester id with the
                                   // stored admin_id.
        const Requester&                     requester,
        const std::optional<jsoncons::json>& permissions_j,
        const std::string&                   service_name,
        Http::Error&                         error);

    /**
     * @brief Finds the requester in the service's `staff` map and decodes
     * its `"<id>:<mask>"` entry.
     * @param[in] requester Caller to look up in the staff map.
     * @param[in] permissions_j Service permissions document.
     * @param[in] service_name Service/table name used in messages.
     * @details Iterates every role's member list; malformed entries raise
     * FORBIDDEN. The returned isMember flag marks a successful match.
     * @param[out] error Receives BAD_REQUEST/FORBIDDEN with a reason on
     * failure.
     * @return The decoded StaffPermission on a match, `std::nullopt`
     * otherwise.
     */
    static std::optional<Permissions::StaffPermission>
    isStaffOfService(  // Finds and decodes the requester's staff entry from
                       // the JSON.
        const Requester&                     requester,
        const std::optional<jsoncons::json>& permissions_j,
        const std::string&                   service_name,
        Http::Error&                         error);

    /**
     * @brief Requires both the requester's group and id to match the target
     * client.
     * @param[in] requester Caller whose group and id are checked.
     * @param[in] groupname Expected client group (typically the entity
     * table name).
     * @param[in] client_id Target client id.
     * @param[out] error Receives FORBIDDEN on mismatch.
     * @return True when both match.
     */
    static bool assert_group_id_match(
        const Requester&   requester,
        const std::string& groupname,
        uint64_t           client_id,
        Http::Error& error);  // Requires both the group and the id to match
                              // the target client.

    /**
     * @brief Validates a service-creation payload: present, valid
     * owner/admin ids, and provider caller.
     * @param[in] requester Provider caller creating the service.
     * @param[in] service_j Creation payload to validate.
     * @param[out] error Receives BAD_REQUEST with a reason on failure.
     * @return True when all preconditions hold.
     */
    static bool preServiceCreateChecks(
        const Requester&                     requester,
        const std::optional<jsoncons::json>& service_j,
        Http::Error& error);  // Validates the payload and caller before a
                              // service is created.

    /**
     * @brief Allows owner/admin unconditionally, or staff carrying the @p
     * required capability bit.
     * @details Think of it as two doors in series: first check if the
     * caller owns or administers the service; only if not, check whether
     *          they are staff with the specific capability bit we need.
     */
    // Owner and admin always pass; other staff need the `required` bit
    // (CAN_READ for reads, CAN_WRITE for create/update, CAN_DELETE for
    // deletes).
    static bool isOwnerOrAdminOrHasPermission(
        const Requester& requester,
        const std::optional<jsoncons::json>&
            permissions_j,  // Allows owner/admin, or staff with the
                            // required capability bit.
        const std::string&      service_name,
        Http::Error&            error,
        Permissions::PowerLevel required = Permissions::PowerLevel::CAN_DELETE);

    /**
     * @brief Allows only the service owner or admin.
     * @param[in] requester Caller whose identity is checked.
     * @param[in] permissions_j Service permissions document.
     * @param[in] service_name Service/table name used in messages.
     * @param[out] error Receives a denial reason from the failing
     * owner/admin check.
     * @return True when either identity check succeeds.
     */
    static bool isOwnerOrAdmin(  // Allows only the owner or the admin.
        const Requester&                     requester,
        const std::optional<jsoncons::json>& permissions_j,
        const std::string&                   service_name,
        Http::Error&                         error);

    /**
     * @brief Extracts and converts @p key from a JSON payload, reporting
     * BAD_REQUEST on failure.
     * @details Returns nullopt (with @p error populated) if the payload is
     * missing/empty or the key is absent or not convertible to T;
     * conversion exceptions are swallowed.
     * @tparam T Target value type (jsoncons `as<T>()`).
     * @param[in] service_j Payload to read.
     * @param[in] key Key to extract.
     * @param[in] service_name Included in the "not provided" message.
     * @param[out] error Receives BAD_REQUEST and a reason on failure.
     * @return The extracted value, or `std::nullopt` on failure.
     */
    template <typename T>  // Generic helper; T is the value type to extract
                           // from the JSON.
    static std::optional<T> extract_json_value_safely(  // Reads a key from JSON
                                                        // as T, reporting
                                                        // BAD_REQUEST instead
                                                        // of throwing.
        const std::optional<jsoncons::json>& service_j,
        const std::string&                   key,
        const std::string&                   service_name,
        Http::Error&                         error)
    {
      if (!service_j.has_value() ||
          service_j->empty())  // Reject a missing or empty payload before
                               // trying to read it.
      {
        error.code = Http::Status::BAD_REQUEST;  // The client sent a bad
                                                 // request; this status is
                                                 // returned to them.
        error.message =
            "Service data is not provided for " +
            service_name;     // Explain which service payload was missing.
        return std::nullopt;  // No usable value; caller must treat this
                              // as failure.
      }

      T value;  // Default-constructed result, filled in only if
                // extraction succeeds.
      try       // Conversion may throw, so it is attempted inside a try block.
      {
        value = service_j->at(key).as<T>();  // Look up the key and convert
                                             // it to the requested type T.
      }
      catch (const std::exception&
                 e)  // Runs when the key is missing or cannot be converted.
      {
        error.message = "Failed to extract " +
                        key;  // Tell the client which key could not be read.
        error.code    = Http::Status::BAD_REQUEST;  // The client sent a bad
                                                    // request; this status is
                                                    // returned to them.
        return std::nullopt;  // No usable value; caller must treat this
                              // as failure.
      }
      return value;  // Hand back the successfully extracted value.
    }

    /**
     * @brief Fetches an entity's permission JSON, serving the idle-expiry
     * cache when possible.
     * @details @p func is invoked with @p args to generate the cache
     * key/query (which embeds the entity table and id). On a cache hit no
     * database access occurs. On a miss the generation token is captured
     * before the read so a concurrent invalidateAll() makes the subsequent
     * put() a no-op. Empty results are not cached.
     * @tparam Func Callable returning `std::optional<std::string>` (the
     * query/key).
     * @tparam Args Argument types forwarded to @p func.
     * @param[in,out] func Query generator.
     * @param[in,out] args Arguments for @p func.
     * @return Cached or freshly read permissions, or `std::nullopt` on no
     * query/no data.
     * @see BasicPermissionCache::generation
     */
    template <typename Func,
              typename... Args>  // Generic helper; Func builds the query and
                                 // Args are its inputs.
    static std::optional<jsoncons::json> getPermissionsOfEntity(
        Func&& func,
        Args&... args)  // Fetches permissions
                        // (cache first, database
                        // on a miss).
    {
      std::optional<std::string> query =
          std::invoke(std::forward<Func>(func),
                      std::forward<Args>(
                          args)...);  // Build the query/key string by calling
                                      // the supplied generator with its
                                      // arguments.

      if (!query.has_value() ||
          query->empty())  // No query means there is nothing to look up;
                           // fail early.
      {
        return std::nullopt;  // No usable value; caller must treat this
                              // as failure.
      }

      auto& cache = PermissionCache::instance();  // Get the shared cache object
                                                  // (a process-wide singleton).
      if (auto cached = cache.get(query.value());
          cached.has_value())  // Cache hit: reuse the stored answer and
                               // skip the database entirely.
      {
        return cached;  // Return the cached JSON to the caller.
      }

      const std::shared_ptr<DatabaseController> db_ctl =
          Store::cachedObject<DatabaseController>();  // Only needed on a cache
                                                      // miss; cachedObject
                                                      // returns a shared
                                                      // pointer kept for reuse.
      const uint64_t generation =
          cache.generation();  // Remember the current epoch before
                               // reading, so a concurrent flush can
                               // invalidate our result.
      std::optional<jsoncons::json> permissions_j = db_ctl->getPermissions(
          query.value());  // Cache miss: read the permissions from
                           // PostgreSQL (blocking version).

      if (!permissions_j.has_value() ||
          permissions_j->empty())  // The database had no data; do not
                                   // cache an empty result.
      {
        return std::nullopt;  // No usable value; caller must treat this
                              // as failure.
      }

      cache.put(query.value(),
                permissions_j.value(),
                generation);  // Store the fresh result, but only if no flush
                              // happened in the meantime.
      return permissions_j;   // Return the freshly read permissions to the
                              // caller.
    }

    /**
     * @brief Coroutine form of getPermissionsOfEntity(); awaits the async
     * database read.
     * @details Same cache-key generation, hit short-circuit and
     * generation-token protection as the synchronous version, but does not
     * block a Drogon IO thread.
     * @tparam Func Callable returning `std::optional<std::string>` (the
     * query/key).
     * @tparam Args Argument types forwarded to @p func.
     * @param[in,out] func Query generator.
     * @param[in,out] args Arguments for @p func.
     * @return Cached or freshly read permissions, or `std::nullopt` on no
     * query/no data.
     * @note The caller must drive the returned task; the query string must
     * outlive the awaited read.
     */
    template <
        typename Func,
        typename... Args>  // Generic helper; Func builds the query and Args
                           // are its inputs.
    static drogon::Task<std::optional<jsoncons::json>>
    getPermissionsOfEntityCoro(Func&& func,
                               Args&... args)  // Coroutine version of the
                                               // fetch; awaits the async
                                               // database read.
    {
      std::optional<std::string> query =
          std::invoke(std::forward<Func>(func),
                      std::forward<Args>(
                          args)...);  // Build the query/key string by calling
                                      // the supplied generator with its
                                      // arguments.

      if (!query.has_value() ||
          query->empty())  // No query means there is nothing to look up;
                           // fail early.
      {
        co_return std::nullopt;  // Coroutine return of 'no value' when
                                 // there is nothing usable.
      }

      auto& cache = PermissionCache::instance();  // Get the shared cache object
                                                  // (a process-wide singleton).
      if (auto cached = cache.get(query.value());
          cached.has_value())  // Cache hit: reuse the stored answer and
                               // skip the database entirely.
      {
        co_return cached;  // Coroutine return of the cached answer.
      }

      const std::shared_ptr<DatabaseController> db_ctl =
          Store::cachedObject<DatabaseController>();
      const uint64_t generation =
          cache.generation();  // Remember the current epoch before the
                               // async read for the same stale-write
                               // protection.
      std::optional<jsoncons::json> permissions_j =
          co_await db_ctl->getPermissionsCoro(
              query.value());  // Cache miss: await the async read without
                               // blocking the IO thread.

      if (!permissions_j.has_value() ||
          permissions_j->empty())  // The database had no data; do not
                                   // cache an empty result.
      {
        co_return std::nullopt;  // Coroutine return of 'no value' when
                                 // there is nothing usable.
      }

      cache.put(query.value(),
                permissions_j.value(),
                generation);    // Store the fresh result, but only if no flush
                                // happened in the meantime.
      co_return permissions_j;  // Coroutine return of the freshly read
                                // answer.
    }

   private:
  };

}  // namespace api::v2
