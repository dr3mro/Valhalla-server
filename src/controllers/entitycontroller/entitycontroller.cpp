/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file entitycontroller.cpp
 * @brief Out-of-line definitions and explicit template instantiations for
 * EntityController<T>.
 * @details The synchronous and coroutine implementations mirror each other:
 * each validates the JSON body, checks GateKeeper authorization, builds the
 * entity from a Types data carrier, and delegates to Controller. Explicit
 * instantiation at the bottom makes these templates available to translation
 * units that include only the header (via gatekeeper/includes.hpp).
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the generic entity pipeline actually lives. Every operation
 * here follows the same shape - parse the JSON, validate it against the table
 * schema, check the caller's permission with GateKeeper, build the entity
 * object, and hand it to Controller for the SQL. The coroutine twins do the
 * same but await the async gate and async DB calls. The bottom of the file
 * lists every concrete entity type so the linker can find these templates;
 * without those instantiations the server would fail to link.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Templates and explicit instantiation:
 * https://en.cppreference.com/w/cpp/language/templates
 * - Coroutines (co_await / co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Lambda expressions (the cached read-schema name):
 * https://en.cppreference.com/w/cpp/language/lambda
 * - Move semantics (std::move):
 * https://en.cppreference.com/w/cpp/language/move_constructor
 * - Exceptions and catch blocks:
 * https://en.cppreference.com/w/cpp/language/exceptions
 * - Callback / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */
#include "controllers/entitycontroller/entitycontroller.hpp"

#include <fmt/core.h>  // Formatting library for the validation error messages.

#include <cstdint>    // For uint64_t entity ids.
#include <exception>  // std::exception, caught around JSON parsing.
#include <format>     // std::format, used to build a cached schema name.
#include <jsoncons/basic_json.hpp>  // JSON parsing and value type.
#include <optional>                 // The optional parsed JSON / target id.
#include <string>                   // std::string for the cached schema name.
#include <string_view>              // Non-owning view of the request body.
#include <unordered_set>            // The requested column set for a read.
#include <utility>                  // std::move for forwarding the callback.

// The persistence methods invoked at the end of each operation.
#include "controllers/base/controller/controller.hpp"
// The interface being implemented.
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
// Drogon Task return type of the Async methods.
#include "drogon/utils/coroutine.h"
#include "entities/base/types.hpp"    // The Create_t/Read_t/... data carriers.
#include "utils/global/callback.hpp"  // Defines CALLBACK_.
#include "utils/global/http.hpp"      // HTTP status constants.
#include "validator/validator.hpp"    // Schema validation helpers and rules.

// Data carriers used to turn validated JSON (plus an id) into an entity
// constructor argument.
using HttpError = api::v2::Http::Error;  // Carries an HTTP code plus a message
                                         // from validation/authorization.
using Create_t  = api::v2::Types::Create_t;  // Carrier holding the validated
                                             // fields for a create.
using Update_t =
    api::v2::Types::Update_t;  // Carrier holding new fields plus the target id.
using Delete_t =
    api::v2::Types::Delete_t;  // Carrier holding just the id to delete.
using Read_t = api::v2::Types::Read_t;  // Carrier holding the column set and id
                                        // for a read.

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Create(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  try
  {
    bool success = false;  // Starts false; validation sets it true only for
                           // a usable body.
    HttpError error;       // Filled with a code/message when validation or
                           // authorization fails.
    // The primary key is server-assigned, so validation must not demand a
    // client-supplied (or nullable) "id".
    static const Validator::Rule rule(
        (Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA |
            Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA),
        {"id"});

    std::optional<jsoncons::json> request_j;  // Will hold the parsed body;
                                              // empty means parsing failed.
    try
    {
      // A malformed body is a client error; reject before authorization
      // or any DB work.
      request_j = jsoncons::json::parse(
          data);  // Turn the raw body text into a JSON value.
    }
    catch (const std::exception&)
    {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
          "Invalid request body: malformed JSON.");
      return;
    }

    if (!request_j.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
      return;
    }

    // Schema validation maps a bad payload to the validator's HTTP error
    // code, without touching the DB.
    success = Validator::validateDatabaseCreateSchema(T::getTableName(),
        request_j, error,
        rule);  // Check the body against the table's create schema.

    if (!success)
    {
      std::move(callback)(error.code,
          fmt::format("Failed to validate request body, {}.", error.message));
      return;
    }

    // Authorization runs before the entity is constructed, so a denied
    // request never reaches the DB.
    if (!gateKeeper->canCreate<T>(requester, request_j, error))
    {
      std::move(callback)(error.code, error.message);
      return;
    }

    // The id is assigned by the database (INSERT ... RETURNING *), so a
    // create is one query, and a request that is rejected above never takes
    // a connection or burns a sequence value.
    Create_t entity_data = Create_t(
        request_j.value());  // Wrap the validated JSON in the create carrier.

    T entity(entity_data);  // Build the entity object the persistence layer
                            // expects.

    Controller::Create(entity,
        std::move(callback));  // Delegate the INSERT; the callback receives
                               // the final answer.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Read(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  try
  {
    jsoncons::json request_j;  // Will hold the parsed read body.
    uint64_t       _id;        // The target entity id extracted from the body.
    std::unordered_set<std::string>
        schema;  // The columns the caller wants returned.
    // A read needs both a numeric id and a column list; missing/invalid
    // fields are answered with 400.
    try
    {
      request_j = jsoncons::json::parse(
          data);  // Turn the raw body text into a JSON value.
      _id =
          request_j.at("id").as<uint64_t>();  // Extract the numeric target id.
      schema = request_j
                   .at("schema")
                   // Extract the list of columns to return.
                   .as<std::unordered_set<std::string>>();
    }
    catch (const std::exception&)
    {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
          "Invalid request body: malformed JSON or missing/invalid "
          "'id'/'schema'.");
      return;
    }
    HttpError error;

    // Ownership/role check runs against the target id before the column
    // list is validated.
    if (!gateKeeper->canRead<T>(requester, _id, error))
    {
      std::move(callback)(error.code, error.message);
      return;
    }

    // Server-managed and sensitive columns may never appear in a
    // client-selected schema.
    static const Validator::Rule rule(
        Validator::Rule::Action::ASSERT_NOT_PRESENT,
        {"id", "username", "password", "created_at", "updated_at"});

    if (!Validator::validateDatabaseReadSchema(
            schema,
            []()
            {
              static const std::string cached =
                  std::format("{}_safe", T::getTableName());
              return cached;
            }(),
            error, rule))
    {
      std::move(callback)(error.code,
          fmt::format("Failed to validate request body, {}.", error.message));
      return;
    }

    T entity((Read_t(schema,
        _id)));  // Wrap the requested columns and id into an entity.

    Controller::Read(entity,
        std::move(callback));  // Delegate the SELECT for the requested columns.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}
template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Update(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data,
    const std::optional<uint64_t> _id)
{
  try
  {
    bool success = false;  // Starts false; validation sets it true only for
                           // a usable body.
    api::v2::Http::Error error;  // Filled with a code/message when
                                 // validation or authorization fails.

    // An update without a target row is meaningless, so reject before
    // parsing the body.
    if (!_id.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::BAD_REQUEST, "No id provided.");
      return;
    }

    std::optional<jsoncons::json> request_json;  // Will hold the parsed body;
                                                 // empty means parsing failed.
    try
    {
      request_json = jsoncons::json::parse(
          data);  // Turn the raw body text into a JSON value.
    }
    catch (const std::exception&)
    {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
          "Invalid request body: malformed JSON.");
      return;
    }

    if (!request_json.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
      return;
    }

    // No exemptions: every submitted field must conform to the entity's
    // update schema.
    static const Validator::Rule rule(Validator::Rule::Action::NONE, {});
    success = Validator::validateDatabaseUpdateSchema(T::getTableName(),
        request_json, error,
        rule);  // Check the body against the table's update schema.

    if (!success)
    {
      std::move(callback)(error.code,
          fmt::format("Failed to validate request body, {}.", error.message));
      return;
    }

    // The requester must be allowed to modify this specific row.
    if (!gateKeeper->canUpdate<T>(requester, _id.value(), error))
    {
      std::move(callback)(error.code, error.message);
      return;
    }

    Update_t entity_data = Update_t(request_json.value(),
        _id.value());  // Wrap validated fields plus the target id.

    T entity(entity_data);  // Build the entity object the persistence layer
                            // expects.

    Controller::Update(
        entity, std::move(callback));  // Delegate the UPDATE statement.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Delete(
    CALLBACK_&& callback, const Requester&& requester,
    const std::optional<uint64_t> _id)
{
  try
  {
    // A delete without a target id is not acceptable (406); no further work
    // is done.
    if (!_id.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
      return;
    }

    HttpError error;

    // Check delete permission on the target row before constructing the
    // entity.
    if (!gateKeeper->canDelete<T>(requester, _id.value(), error))
    {
      std::move(callback)(error.code, error.message);
      return;
    }

    T entity(
        Delete_t(_id.value()));  // Wrap the id into a minimal delete entity.

    Controller::Delete(
        entity, std::move(callback));  // Delegate the DELETE statement.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Search(
    CALLBACK_&& callback, [[maybe_unused]] const Requester&& requester,
    std::string_view data)
{
  jsoncons::json request_json;  // Will hold the parsed search body.
  try
  {
    // Malformed JSON is a client error; answer 400 rather than letting the
    // parser exception escape.
    request_json = jsoncons::json::parse(
        data);  // Turn the raw search body text into a JSON value.
  }
  catch (const std::exception&)
  {
    std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
        "Invalid request body: malformed JSON.");
    return;
  }
  try
  {
    bool success = false;  // Search carrier sets this true only when the
                           // criteria parse successfully.
    // Search_t validates and binds all criteria; success=false means the
    // body was unusable, so never run the query.
    T    entity((Search_t(request_json,
        success)));     // The carrier parses criteria and reports success via
                        // the flag.

    if (success)
    {
      Controller::Search(entity,
          std::move(callback));  // Delegate the paginated search query.
    }
    else
    {
      std::move(callback)(
          api::v2::Http::Status::NOT_ACCEPTABLE, "Failed to search");
    }
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <typename T>
drogon::Task<void> EntityController<T>::CreateAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  try
  {
    bool success = false;  // Starts false; validation sets it true only for
                           // a usable body.
    HttpError error;       // Filled with a code/message when validation or
                           // authorization fails.
    // The primary key is server-assigned, so validation must not demand a
    // client-supplied (or nullable) "id".
    static const Validator::Rule rule(
        (Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA |
            Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA),
        {"id"});

    std::optional<jsoncons::json> request_j;  // Will hold the parsed body;
                                              // empty means parsing failed.
    try
    {
      // A malformed body is a client error; reject before authorization
      // or any DB work.
      request_j = jsoncons::json::parse(
          data);  // Turn the raw body text into a JSON value.
    }
    catch (const std::exception&)
    {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
          "Invalid request body: malformed JSON.");
      co_return;
    }

    if (!request_j.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
      co_return;
    }

    // Schema validation maps a bad payload to the validator's HTTP error
    // code, without touching the DB.
    success = Validator::validateDatabaseCreateSchema(T::getTableName(),
        request_j, error,
        rule);  // Check the body against the table's create schema.

    if (!success)
    {
      std::move(callback)(error.code,
          fmt::format("Failed to validate request body, {}.", error.message));
      co_return;
    }

    // The async gate check is awaited on the IO loop; a denial still
    // short-circuits before any DB work.
    if (!co_await gateKeeper->canCreateAsync<T>(requester, request_j, error))
    {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    Create_t entity_data = Create_t(
        request_j.value());  // Wrap the validated JSON in the create carrier.

    T entity(entity_data);  // Build the entity object the persistence layer
                            // expects.

    co_await Controller::CreateAsync(entity,
        std::move(callback));  // Await the async INSERT before finishing.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}
template <typename T>
drogon::Task<void> EntityController<T>::ReadAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  try
  {
    jsoncons::json request_j;  // Will hold the parsed read body.
    uint64_t       _id;        // The target entity id extracted from the body.
    std::unordered_set<std::string>
        schema;  // The columns the caller wants returned.
    // A read needs both a numeric id and a column list; missing/invalid
    // fields are answered with 400.
    try
    {
      request_j = jsoncons::json::parse(
          data);  // Turn the raw body text into a JSON value.
      _id =
          request_j.at("id").as<uint64_t>();  // Extract the numeric target id.
      schema = request_j
                   .at("schema")
                   // Extract the list of columns to return.
                   .as<std::unordered_set<std::string>>();
    }
    catch (const std::exception&)
    {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
          "Invalid request body: malformed JSON or missing/invalid "
          "'id'/'schema'.");
      co_return;
    }
    HttpError error;

    // Ownership/role check is awaited before the column list is validated.
    if (!co_await gateKeeper->canReadAsync<T>(requester, _id, error))
    {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    // Server-managed and sensitive columns may never appear in a
    // client-selected schema.
    static const Validator::Rule rule(
        Validator::Rule::Action::ASSERT_NOT_PRESENT,
        {"id", "username", "password", "created_at", "updated_at"});

    if (!Validator::validateDatabaseReadSchema(
            schema,
            []()
            {
              static const std::string cached =
                  std::format("{}_safe", T::getTableName());
              return cached;
            }(),
            error, rule))
    {
      std::move(callback)(error.code,
          fmt::format("Failed to validate request body, {}.", error.message));
      co_return;
    }

    T entity((Read_t(schema,
        _id)));  // Wrap the requested columns and id into an entity.

    co_await Controller::ReadAsync(
        entity, std::move(callback));  // Await the async SELECT.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}
template <typename T>
drogon::Task<void> EntityController<T>::UpdateAsync(CALLBACK_&& callback,
    const Requester&& requester, std::string_view data,
    const std::optional<uint64_t> _id)
{
  try
  {
    bool success = false;  // Starts false; validation sets it true only for
                           // a usable body.
    api::v2::Http::Error error;  // Filled with a code/message when
                                 // validation or authorization fails.

    // An update without a target row is meaningless, so reject before
    // parsing the body.
    if (!_id.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::BAD_REQUEST, "No id provided.");
      co_return;
    }

    std::optional<jsoncons::json> request_json;  // Will hold the parsed body;
                                                 // empty means parsing failed.
    try
    {
      request_json = jsoncons::json::parse(
          data);  // Turn the raw body text into a JSON value.
    }
    catch (const std::exception&)
    {
      std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
          "Invalid request body: malformed JSON.");
      co_return;
    }

    if (!request_json.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
      co_return;
    }

    // No exemptions: every submitted field must conform to the entity's
    // update schema.
    static const Validator::Rule rule(Validator::Rule::Action::NONE, {});
    success = Validator::validateDatabaseUpdateSchema(T::getTableName(),
        request_json, error,
        rule);  // Check the body against the table's update schema.

    if (!success)
    {
      std::move(callback)(error.code,
          fmt::format("Failed to validate request body, {}.", error.message));
      co_return;
    }

    // The requester must be allowed to modify this specific row.
    if (!co_await gateKeeper->canUpdateAsync<T>(requester, _id.value(), error))
    {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    Update_t entity_data = Update_t(request_json.value(),
        _id.value());  // Wrap validated fields plus the target id.

    T entity(entity_data);  // Build the entity object the persistence layer
                            // expects.

    co_await Controller::UpdateAsync(
        entity, std::move(callback));  // Await the async UPDATE.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}
template <typename T>
drogon::Task<void> EntityController<T>::DeleteAsync(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> _id)
{
  try
  {
    // A delete without a target id is not acceptable (406); no further work
    // is done.
    if (!_id.has_value())
    {
      std::move(callback)(
          api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
      co_return;
    }

    HttpError error;

    // Check delete permission on the target row before constructing the
    // entity.
    if (!co_await gateKeeper->canDeleteAsync<T>(requester, _id.value(), error))
    {
      std::move(callback)(error.code, error.message);
      co_return;
    }

    T entity(
        Delete_t(_id.value()));  // Wrap the id into a minimal delete entity.

    co_await Controller::DeleteAsync(
        entity, std::move(callback));  // Await the async DELETE.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}
template <typename T>
drogon::Task<void> EntityController<T>::SearchAsync(CALLBACK_&& callback,
    [[maybe_unused]] const Requester&& requester, std::string_view data)
{
  jsoncons::json request_json;  // Will hold the parsed search body.
  try
  {
    // Malformed JSON is a client error; answer 400 rather than letting the
    // parser exception escape.
    request_json = jsoncons::json::parse(
        data);  // Turn the raw search body text into a JSON value.
  }
  catch (const std::exception&)
  {
    std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
        "Invalid request body: malformed JSON.");
    co_return;
  }
  try
  {
    bool success = false;  // Search carrier sets this true only when the
                           // criteria parse successfully.
    // Search_t validates and binds all criteria; success=false means the
    // body was unusable, so never run the query.
    T    entity((Search_t(request_json,
        success)));     // The carrier parses criteria and reports success via
                        // the flag.

    if (success)
    {
      co_await Controller::SearchAsync(
          entity, std::move(callback));  // Await the async search.
    }
    else
    {
      std::move(callback)(
          api::v2::Http::Status::NOT_ACCEPTABLE, "Failed to search");
    }
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}
/**
 * @def INSTANTIATE_ENTITY_CONTROLLER(TYPE)
 * @brief Forces compilation of both sync and coroutine CRUDS members for one
 * entity type.
 * @details Required because template definitions live in this translation unit;
 * without these instantiations, callers including only the header would fail to
 * link. The entity types are listed via gatekeeper/includes.hpp.
 */
#define INSTANTIATE_ENTITY_CONTROLLER(TYPE) /*NOLINT*/               \
  template void EntityController<TYPE>::Create(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data);           \
  template void EntityController<TYPE>::Read(CALLBACK_&& callback,   \
      const Requester&& requester, std::string_view data);           \
  template void EntityController<TYPE>::Update(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data,            \
      std::optional<uint64_t> id);                                   \
  template void EntityController<TYPE>::Delete(CALLBACK_&& callback, \
      const Requester&& requester, std::optional<uint64_t> id);      \
  template void EntityController<TYPE>::Search(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data);           \
  template drogon::Task<void> EntityController<TYPE>::CreateAsync(   \
      CALLBACK_&& callback, const Requester&& requester,             \
      std::string_view data);                                        \
  template drogon::Task<void> EntityController<TYPE>::ReadAsync(     \
      CALLBACK_&& callback, const Requester&& requester,             \
      std::string_view data);                                        \
  template drogon::Task<void> EntityController<TYPE>::UpdateAsync(   \
      CALLBACK_&& callback, const Requester&& requester,             \
      std::string_view data, std::optional<uint64_t> id);            \
  template drogon::Task<void> EntityController<TYPE>::DeleteAsync(   \
      CALLBACK_&& callback, const Requester&& requester,             \
      std::optional<uint64_t> id);                                   \
  template drogon::Task<void> EntityController<TYPE>::SearchAsync(   \
      CALLBACK_&& callback, const Requester&& requester,             \
      std::string_view data);

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep
// Instantiate for all entity types
INSTANTIATE_ENTITY_CONTROLLER(User)              // A client/person entity.
INSTANTIATE_ENTITY_CONTROLLER(Provider)          // A client/person entity.
INSTANTIATE_ENTITY_CONTROLLER(Clinics)           // A service/facility entity.
INSTANTIATE_ENTITY_CONTROLLER(Pharmacies)        // A service/facility entity.
INSTANTIATE_ENTITY_CONTROLLER(Laboratories)      // A service/facility entity.
INSTANTIATE_ENTITY_CONTROLLER(RadiologyCenters)  // A service/facility entity.
INSTANTIATE_ENTITY_CONTROLLER(Patient)        // A clinic entity (has visits).
INSTANTIATE_ENTITY_CONTROLLER(Health)         // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(PatientDrugs)   // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(Reports)        // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(Visits)         // A clinic entity (has visits).
INSTANTIATE_ENTITY_CONTROLLER(VisitDrugs)     // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(Prescriptions)  // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(Requests)       // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(PaidServices)   // A clinic entity.
INSTANTIATE_ENTITY_CONTROLLER(ClinicAppointment)      // An appointment entity.
INSTANTIATE_ENTITY_CONTROLLER(PharmacyAppointment)    // An appointment entity.
INSTANTIATE_ENTITY_CONTROLLER(LaboratoryAppointment)  // An appointment entity.
INSTANTIATE_ENTITY_CONTROLLER(
    RadiologyCenterAppointment)  // An appointment entity.
