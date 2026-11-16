/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file cliniccontroller.cpp
 * @brief Definitions and explicit instantiations for ClinicController<T> over
 * clinic-service entity types.
 * @details Constrained `*Impl` helpers select behaviour per type:
 * client-supplied ids for most entities, generic creation for Patient/Visits,
 * and "not implemented" responses for unsupported Delete/Search/GetVisits.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the type-specific clinic rules are implemented. The non-Patient
 * create path carefully parses a client-supplied id (with its own checks so bad
 * input becomes a 400, not a 500), while Patient/Visits reuse the generic flow.
 * Unsupported actions get an explicit client error instead of a bogus database
 * call. The public methods at the bottom simply forward to the right helper,
 * and the instantiations make all clinic types linkable.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Requires clauses / constraints (C++20):
 * https://en.cppreference.com/w/cpp/language/requires
 * - Templates and explicit instantiation:
 * https://en.cppreference.com/w/cpp/language/templates
 * - Coroutines (co_await / co_return):
 * https://en.cppreference.com/w/cpp/language/coroutines
 * - Exceptions and catch blocks:
 * https://en.cppreference.com/w/cpp/language/exceptions
 * - Callback / std::function:
 * https://en.cppreference.com/w/cpp/utility/functional/function
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */
// The class being defined.
#include "controllers/cliniccontroller/cliniccontroller.hpp"

// Generic CRUDS methods reused by several helpers.
#include "controllers/entitycontroller/entitycontroller.hpp"

template <typename T>
template <typename U>
void ClinicController<T>::CreateImpl(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
  requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
{
  jsoncons::json request_json;  // Will hold the parsed create body.
  try
  {
    request_json = jsoncons::json::parse(
        data);  // Turn the raw body into JSON; malformed text throws.
  }
  catch (const std::exception&)
  {
    callback(api::v2::Http::Status::BAD_REQUEST,
        "Invalid request body: malformed JSON.");
    return;
  }
  bool success = false;  // Validation flips this true only for a usable body.

  // Unlike clients, these entities carry a client-supplied id, so "id" is
  // exempt from the normal schema rules and checked explicitly below.
  static const Validator::Rule rule(
      (Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA |
          Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA),
      {"id"});

  api::v2::Http::Error
      error;  // Receives a code/message if authorization fails.
  try
  {
    success = Validator::validateDatabaseCreateSchema(T::getTableName(),
        request_json, error, rule);  // Schema check with 'id' exempted.
    if (!success)
    {
      callback(error.code, error.message);
      return;
    }

    // A body that simply omits "id" (e.g. {"reports": [...], "clinic_id":
    // ...}) is a client mistake, not a server failure - .at("id") below
    // throws "Key not found" for exactly that case, and
    // validateDatabaseCreateSchema() above was told to ignore a missing
    // "id" (see `rule`), so it has to be checked explicitly here before the
    // .at() call, or it falls through uncaught into the generic 500 handler
    // below.
    if (!request_json.contains("id"))
    {
      callback(api::v2::Http::Status::BAD_REQUEST, "id not provided.");
      return;
    }
    // A fuzzer-sized "id" (e.g. 33689528311627667466646192128, far past
    // UINT64_MAX) makes jsoncons' .as<uint64_t>() throw "Integer overflow"
    // rather than just failing to convert - that's still a
    // malformed-client-input case, not a server fault, so it needs its own
    // catch here rather than falling through to the generic 500 handler
    // below.
    std::optional<uint64_t> id;  // Explicit client-supplied id, filled below.
    try
    {
      id = request_json.at("id").as<uint64_t>();  // Read the id as a 64-bit
                                                  // unsigned integer.
    }
    catch (const std::exception&)
    {
      callback(api::v2::Http::Status::BAD_REQUEST,
          "id must be a non-negative integer that fits in 64 bits.");
      return;
    }
    if (!id.has_value())
    {
      callback(api::v2::Http::Status::BAD_REQUEST, "id not provided.");
      return;
    }

    // Authorize before constructing the entity and issuing the write.
    if (!gateKeeper->canCreate<T>(requester, request_json, error))
    {
      callback(error.code, error.message);
      return;
    }

    // Pass the validated id explicitly: this entity type does not rely on a
    // database-generated key.
    Types::Create_t clinic_create_data = Types::Create_t(
        request_json, id.value());  // Wrap body plus explicit id.

    T entity(clinic_create_data);  // Build the entity that will be persisted.

    Controller::Create(entity,
        std::move(callback));  // Delegate the INSERT to the persistence engine.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <typename T>
template <typename U>
void ClinicController<T>::CreateImpl(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
  requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
{
  // Patient/Visits use the server-assigned-id path; no explicit id parsing is
  // required.
  EntityController<T>::Create(std::move(callback), std::move(requester),
      data);  // Patient/Visits need no special id handling.
}

template <typename T>
template <typename U>
drogon::Task<void> ClinicController<T>::CreateAsyncImpl(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
  requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
{
  // Patient/Visits reuse the generic async create flow.
  co_await EntityController<T>::CreateAsync(std::move(callback),
      std::move(requester), data);  // Reuse the generic async create.
}

template <typename T>
template <typename U>
drogon::Task<void> ClinicController<T>::CreateAsyncImpl(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
  requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
{
  jsoncons::json request_json;  // Will hold the parsed create body.
  try
  {
    request_json = jsoncons::json::parse(
        data);  // Turn the raw body into JSON; malformed text throws.
  }
  catch (const std::exception&)
  {
    callback(api::v2::Http::Status::BAD_REQUEST,
        "Invalid request body: malformed JSON.");
    co_return;
  }
  bool success = false;  // Validation flips this true only for a usable body.

  // Unlike clients, these entities carry a client-supplied id, so "id" is
  // exempt from the normal schema rules and checked explicitly below.
  static const Validator::Rule rule(
      (Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA |
          Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA),
      {"id"});

  api::v2::Http::Error
      error;  // Receives a code/message if authorization fails.
  try
  {
    success = Validator::validateDatabaseCreateSchema(T::getTableName(),
        request_json, error, rule);  // Schema check with 'id' exempted.
    if (!success)
    {
      callback(error.code, error.message);
      co_return;
    }

    // A body that simply omits "id" (e.g. {"reports": [...], "clinic_id":
    // ...}) is a client mistake, not a server failure - .at("id") below
    // throws "Key not found" for exactly that case, and
    // validateDatabaseCreateSchema() above was told to ignore a missing
    // "id" (see `rule`), so it has to be checked explicitly here before the
    // .at() call, or it falls through uncaught into the generic 500 handler
    // below.
    if (!request_json.contains("id"))
    {
      callback(api::v2::Http::Status::BAD_REQUEST, "id not provided.");
      co_return;
    }
    // A fuzzer-sized "id" (e.g. 33689528311627667466646192128, far past
    // UINT64_MAX) makes jsoncons' .as<uint64_t>() throw "Integer overflow"
    // rather than just failing to convert - that's still a
    // malformed-client-input case, not a server fault, so it needs its own
    // catch here rather than falling through to the generic 500 handler
    // below.
    std::optional<uint64_t> id;  // Explicit client-supplied id, filled below.
    try
    {
      id = request_json.at("id").as<uint64_t>();  // Read the id as a 64-bit
                                                  // unsigned integer.
    }
    catch (const std::exception&)
    {
      callback(api::v2::Http::Status::BAD_REQUEST,
          "id must be a non-negative integer that fits in 64 bits.");
      co_return;
    }
    if (!id.has_value())
    {
      callback(api::v2::Http::Status::BAD_REQUEST, "id not provided.");
      co_return;
    }

    // Async authorization gate; a denial short-circuits before any write.
    if (!co_await gateKeeper->canCreateAsync<T>(requester, request_json, error))
    {
      callback(error.code, error.message);
      co_return;
    }

    // Pass the validated id explicitly: this entity type does not rely on a
    // database-generated key.
    Types::Create_t clinic_create_data = Types::Create_t(
        request_json, id.value());  // Wrap body plus explicit id.

    T entity(clinic_create_data);  // Build the entity that will be persisted.

    co_await Controller::CreateAsync(
        entity, std::move(callback));  // Await the async INSERT.
    co_return;
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
        co_return;
  }
}

template <typename T>
template <typename U>
void ClinicController<T>::DeleteImpl(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> id)
  requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
{
  EntityController<T>::Delete(std::move(callback), std::move(requester),
      id);  // Patient/Visits support deletion.
}

template <typename T>
template <typename U>
void ClinicController<T>::DeleteImpl(CALLBACK_&& callback,
    [[maybe_unused]] const Requester&&           requester,
    const std::optional<uint64_t>                id)
  requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
{
  // Deletion is deliberately unsupported for these clinic entity types; never
  // issue a DB statement.
  (void)id;  // Explicitly mark the unused id as intentionally ignored.
  callback(api::v2::Http::Status::BAD_REQUEST,
      fmt::format("Delete is NOT implemented for entity type {}",
          T::getTableName()));  // Clear client error, no DB work.
}

template <typename T>
template <typename U>
drogon::Task<void> ClinicController<T>::DeleteAsyncImpl(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> id)
  requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
{
  co_await EntityController<T>::DeleteAsync(std::move(callback),
      std::move(requester), id);  // Await the generic async delete.
}

template <typename T>
template <typename U>
drogon::Task<void> ClinicController<T>::DeleteAsyncImpl(CALLBACK_&& callback,
    [[maybe_unused]] const Requester&&                              requester,
    const std::optional<uint64_t>                                   id)
  requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
{
  // Async counterpart of the unsupported-delete path.
  (void)id;  // Explicitly mark the unused id as intentionally ignored.
  std::move(callback)(api::v2::Http::Status::BAD_REQUEST,
      fmt::format("Delete is NOT implemented for entity type {}",
          T::getTableName()));  // Async not-implemented answer.
  co_return;
}

template <typename T>
template <typename U>
void ClinicController<T>::SearchImpl(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
  requires(std::is_same<U, Patient>::value)
{
  EntityController<T>::Search(std::move(callback), std::move(requester),
      data);  // Only Patient reaches this path.
}

template <typename T>
template <typename U>
void ClinicController<T>::SearchImpl(CALLBACK_&& callback,
    [[maybe_unused]] const Requester&&           requester,
    [[maybe_unused]] std::string_view            data)
  requires(!std::is_same<U, Patient>::value)
{
  // Only Patient supports search; other clinic entities get a clear client
  // error instead of a DB query.
  callback(api::v2::Http::Status::BAD_REQUEST,
      fmt::format("Search is NOT implemented for entity type {}",
          T::getTableName()));  // Other types get a client error.
}

template <typename T>
template <typename U>
void ClinicController<T>::GetVisitsImpl(CALLBACK_&& callback,
    const Requester&& requester, std::optional<uint64_t> patient_id)
  requires(std::is_same<U, Patient>::value)
{
  Http::Error error;  // Receives a code/message if authorization fails.
  try
  {
    // Without a patient id there is no visit history to fetch; reject with
    // 400 before authorization.
    if (!patient_id.has_value())
    {
      callback(api::v2::Http::Status::BAD_REQUEST, "Missing patient id.");
      return;
    }
    T entity((Types::Data_t(
        patient_id.value())));  // Minimal entity carrying the patient id.

    // Reuse the read permission: visits are sensitive patient data.
    if (!gateKeeper->canRead<T>(requester, patient_id.value(), error))
    {
      callback(error.code, error.message);
      return;
    }

    Controller::GetVisits(
        entity, std::move(callback));  // Delegate the visit lookup query.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
}

template <typename T>
template <typename U>
void ClinicController<T>::GetVisitsImpl(CALLBACK_&& callback,
    [[maybe_unused]] const Requester&&              requester,
    [[maybe_unused]] const std::optional<uint64_t>  patient_id)
  requires(!std::is_same<U, Patient>::value)
{
  // Visit history is a patient-only concept.
  callback(api::v2::Http::Status::BAD_REQUEST,
      fmt::format("GetVisit is NOT implemented for entity type {}",
          T::getTableName()));  // Visit history is patient-only.
}

// Public overrides are thin forwarders: Create/Delete/Search/GetVisits dispatch
// to the constrained Impl helpers, while Read/Update reuse EntityController's
// generic implementations.
template <typename T>
void ClinicController<T>::Create(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  CreateImpl(std::move(callback), std::move(requester),
      data);  // Choose the right create helper at compile time.
}

template <typename T>
drogon::Task<void> ClinicController<T>::CreateAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  co_await CreateAsyncImpl(std::move(callback), std::move(requester),
      data);  // Coroutine create dispatch.
}

template <typename T>
void ClinicController<T>::Read(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  EntityController<T>::Read(std::move(callback), std::move(requester),
      data);  // Read is generic for all clinic types.
}

template <typename T>
drogon::Task<void> ClinicController<T>::ReadAsync(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  co_await EntityController<T>::ReadAsync(std::move(callback),
      std::move(requester), data);  // Coroutine read reuse.
}

template <typename T>
void ClinicController<T>::Update(CALLBACK_&& callback,
    const Requester&& requester, std::string_view data,
    const std::optional<uint64_t> id)
{
  EntityController<T>::Update(std::move(callback), std::move(requester), data,
      id);  // Update is generic for all clinic types.
}

template <typename T>
drogon::Task<void> ClinicController<T>::UpdateAsync(CALLBACK_&& callback,
    const Requester&& requester, std::string_view data,
    const std::optional<uint64_t> id)
{
  co_await EntityController<T>::UpdateAsync(std::move(callback),
      std::move(requester), data, id);  // Coroutine update reuse.
}

template <typename T>
void ClinicController<T>::Delete(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> id)
{
  DeleteImpl(std::move(callback), std::move(requester),
      id);  // Choose supported vs not-implemented delete.
}

template <typename T>
drogon::Task<void> ClinicController<T>::DeleteAsync(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> id)
{
  co_await DeleteAsyncImpl(std::move(callback), std::move(requester),
      id);  // Coroutine delete dispatch.
}

template <typename T>
void ClinicController<T>::Search(
    CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
  SearchImpl(std::move(callback), std::move(requester),
      data);  // Patient vs not-implemented search.
}

template <typename T>
void ClinicController<T>::GetVisits(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> patient_id)
{
  GetVisitsImpl(std::move(callback), std::move(requester),
      patient_id);  // Patient vs not-implemented visit lookup.
}

template <typename T>
template <typename U>
drogon::Task<void> ClinicController<T>::GetVisitsAsyncImpl(CALLBACK_&& callback,
    const Requester&& requester, std::optional<uint64_t> patient_id)
  requires(std::is_same<U, Patient>::value)
{
  Http::Error error;  // Receives a code/message if authorization fails.
  try
  {
    // Without a patient id there is no visit history to fetch; reject with
    // 400 before authorization.
    if (!patient_id.has_value())
    {
      callback(api::v2::Http::Status::BAD_REQUEST, "Missing patient id.");
      co_return;
    }
    T entity((Types::Data_t(
        patient_id.value())));  // Minimal entity carrying the patient id.

    // Await the read permission before loading visits.
    if (!co_await gateKeeper->canReadAsync<T>(
            requester, patient_id.value(), error))
    {
      callback(error.code, error.message);
      co_return;
    }

    co_await Controller::GetVisitsAsync(
        entity, std::move(callback));  // Await the async visit lookup.
  }
  catch (const std::exception& e)
  {
    CRITICALMESSAGERESPONSE  // Unexpected error: log it and answer with a
                             // mapped 5xx status.
  }
  co_return;
}

template <typename T>
template <typename U>
drogon::Task<void> ClinicController<T>::GetVisitsAsyncImpl(CALLBACK_&& callback,
    [[maybe_unused]] const Requester&&             requester,
    [[maybe_unused]] const std::optional<uint64_t> patient_id)
  requires(!std::is_same<U, Patient>::value)
{
  // Visit history is a patient-only concept.
  callback(api::v2::Http::Status::BAD_REQUEST,
      fmt::format("GetVisit is NOT implemented for entity type {}",
          T::getTableName()));  // Visit history is patient-only.
  co_return;
}

template <typename T>
drogon::Task<void> ClinicController<T>::GetVisitsAsync(CALLBACK_&& callback,
    const Requester&& requester, const std::optional<uint64_t> patient_id)
{
  co_await GetVisitsAsyncImpl(std::move(callback), std::move(requester),
      patient_id);  // Coroutine visit dispatch.
}

/**
 * @def INSTANTIATE_CLINIC_CONTROLLER(TYPE)
 * @brief Instantiates every ClinicController member (public overrides and
 * constrained Impl helpers) for one type.
 * @details Definitions live in this translation unit, so explicit
 * instantiations are required for callers that include only the header. The
 * supported clinic entity types are listed below.
 */
#define INSTANTIATE_CLINIC_CONTROLLER(TYPE)                                      \
  template void ClinicController<TYPE>::CreateImpl(CALLBACK_&& callback,         \
      const Requester&& requester, std::string_view data);                       \
  template drogon::Task<void> ClinicController<TYPE>::CreateAsyncImpl(           \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::string_view data);                                                    \
  template void ClinicController<TYPE>::DeleteImpl(CALLBACK_&& callback,         \
      const Requester&& requester, std::optional<uint64_t> id);                  \
  template drogon::Task<void> ClinicController<TYPE>::DeleteAsyncImpl(           \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::optional<uint64_t> id);                                               \
  template void ClinicController<TYPE>::SearchImpl(CALLBACK_&& callback,         \
      const Requester&& requester, std::string_view data);                       \
  template void ClinicController<TYPE>::GetVisitsImpl(CALLBACK_&& callback,      \
      const Requester&& requester, std::optional<uint64_t> patient_id);          \
  template void ClinicController<TYPE>::Create(CALLBACK_&& callback,             \
      const Requester&& requester, std::string_view data);                       \
  template drogon::Task<void> ClinicController<TYPE>::CreateAsync(               \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::string_view data);                                                    \
  template void               ClinicController<TYPE>::Read(CALLBACK_&& callback, \
      const Requester&& requester, std::string_view data);                       \
  template drogon::Task<void> ClinicController<TYPE>::ReadAsync(                 \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::string_view data);                                                    \
  template void ClinicController<TYPE>::Update(CALLBACK_&& callback,             \
      const Requester&& requester, std::string_view data,                        \
      std::optional<uint64_t> id);                                               \
  template drogon::Task<void> ClinicController<TYPE>::UpdateAsync(               \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::string_view data, std::optional<uint64_t> id);                        \
  template void ClinicController<TYPE>::Delete(CALLBACK_&& callback,             \
      const Requester&& requester, std::optional<uint64_t> id);                  \
  template drogon::Task<void> ClinicController<TYPE>::DeleteAsync(               \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::optional<uint64_t> id);                                               \
  template void ClinicController<TYPE>::Search(CALLBACK_&& callback,             \
      const Requester&& requester, std::string_view data);                       \
  template void ClinicController<TYPE>::GetVisits(CALLBACK_&& callback,          \
      const Requester&& requester, std::optional<uint64_t> patient_id);          \
  template drogon::Task<void> ClinicController<TYPE>::GetVisitsAsync(            \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::optional<uint64_t> patient_id);                                       \
  template drogon::Task<void> ClinicController<TYPE>::GetVisitsAsyncImpl(        \
      CALLBACK_&& callback, const Requester&& requester,                         \
      std::optional<uint64_t> patient_id);

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep
// Instantiate for all entity types

INSTANTIATE_CLINIC_CONTROLLER(Patient)        // Clinic entity with visits.
INSTANTIATE_CLINIC_CONTROLLER(Health)         // Clinic entity.
INSTANTIATE_CLINIC_CONTROLLER(PatientDrugs)   // Clinic entity.
INSTANTIATE_CLINIC_CONTROLLER(Reports)        // Clinic entity.
INSTANTIATE_CLINIC_CONTROLLER(Visits)         // Clinic entity with visits.
INSTANTIATE_CLINIC_CONTROLLER(VisitDrugs)     // Clinic entity.
INSTANTIATE_CLINIC_CONTROLLER(Prescriptions)  // Clinic entity.
INSTANTIATE_CLINIC_CONTROLLER(Requests)       // Clinic entity.
INSTANTIATE_CLINIC_CONTROLLER(PaidServices)   // Clinic entity.
