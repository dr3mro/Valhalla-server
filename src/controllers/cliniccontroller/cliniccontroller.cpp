#include "controllers/cliniccontroller/cliniccontroller.hpp"

template <typename T>
template <typename U>
void ClinicController<T>::CreateImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
    requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
{
    jsoncons::json request_json = jsoncons::json::parse(data);
    bool           success      = false;

    Validator::Rule rule((Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA | Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA), {"id"});

    api::v2::Http::Error error;
    try
    {
        success = Validator::validateDatabaseCreateSchema(T::getTableName(), request_json, error, rule);
        if (!success)
        {
            callback(error.code, error.message);
            return;
        }

        std::optional<uint64_t> id = request_json.at("id").as<uint64_t>();
        if (!id.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "id not provided.");
            return;
        }

        Types::Create_t clinic_create_data = Types::Create_t(request_json, id.value());

        T entity(clinic_create_data);

        if (!gateKeeper->canCreate<T>(requester, T::getTableName(), request_json, error))
        {
            callback(error.code, error.message);
            return;
        }

        Controller::Create(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
template <typename U>
void ClinicController<T>::CreateImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
    requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
{
    EntityController<T>::Create(std::move(callback), std::move(requester), data);
}

template <typename T>
template <typename U>
void ClinicController<T>::DeleteImpl(CALLBACK_ &&callback, const Requester &&requester, const std::optional<uint64_t> id)
    requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
{
    EntityController<T>::Delete(std::move(callback), std::move(requester), id);
}

template <typename T>
template <typename U>
void ClinicController<T>::DeleteImpl(CALLBACK_ &&callback, [[maybe_unused]] const Requester &&requester, const std::optional<uint64_t> id)
    requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
{
    (void)id;
    callback(api::v2::Http::Status::BAD_REQUEST, fmt::format("Delete is NOT implemented for entity type {}", T::getTableName()));
}

template <typename T>
template <typename U>
void ClinicController<T>::SearchImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
    requires(std::is_same<U, Patient>::value)
{
    EntityController<T>::Search(std::move(callback), std::move(requester), data);
}

template <typename T>
template <typename U>
void ClinicController<T>::SearchImpl(CALLBACK_ &&callback, [[maybe_unused]] const Requester &&requester, [[maybe_unused]] std::string_view data)
    requires(!std::is_same<U, Patient>::value)
{
    callback(api::v2::Http::Status::BAD_REQUEST, fmt::format("Search is NOT implemented for entity type {}", T::getTableName()));
}

template <typename T>
template <typename U>
void ClinicController<T>::GetVisitsImpl(CALLBACK_ &&callback, const Requester &&requester, const std::optional<uint64_t> id)
    requires(std::is_same<U, Patient>::value)
{
    Http::Error error;
    try
    {
        if (!id.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "Missing patient id.");
            return;
        }
        T entity((Types::Data_t(id.value())));

        if (!gateKeeper->canRead<T>(requester, T::getTableName(), id.value(), error))
        {
            callback(error.code, error.message);
            return;
        }

        Controller::GetVisits(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
template <typename U>
void ClinicController<T>::GetVisitsImpl(CALLBACK_ &&callback, [[maybe_unused]] const Requester &&requester, const std::optional<uint64_t> id)
    requires(!std::is_same<U, Patient>::value)
{
    (void)id;
    callback(api::v2::Http::Status::BAD_REQUEST, fmt::format("GetVisit is NOT implemented for entity type {}", T::getTableName()));
}

template <typename T>
void ClinicController<T>::Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    CreateImpl(std::move(callback), std::move(requester), data);
}

template <typename T>
void ClinicController<T>::Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    EntityController<T>::Read(std::move(callback), std::move(requester), data);
}

template <typename T>
void ClinicController<T>::Update(CALLBACK_ &&callback, const Requester &&requester, std::string_view data, const std::optional<uint64_t> id)
{
    EntityController<T>::Update(std::move(callback), std::move(requester), data, id);
}

template <typename T>
void ClinicController<T>::Delete(CALLBACK_ &&callback, const Requester &&requester, const std::optional<uint64_t> id)
{
    DeleteImpl(std::move(callback), std::move(requester), id);
}

template <typename T>
void ClinicController<T>::Search(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    SearchImpl(std::move(callback), std::move(requester), data);
}

template <typename T>
void ClinicController<T>::GetVisits(CALLBACK_ &&callback, const Requester &&requester, const std::optional<uint64_t> id)
{
    GetVisitsImpl(std::move(callback), std::move(requester), id);
}

#define INSTANTIATE_CLINIC_CONTROLLER(TYPE)                                                                                                             \
    template void ClinicController<TYPE>::CreateImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                         \
    template void ClinicController<TYPE>::DeleteImpl(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id);                    \
    template void ClinicController<TYPE>::SearchImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                         \
    template void ClinicController<TYPE>::GetVisitsImpl(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id);                 \
    template void ClinicController<TYPE>::Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                             \
    template void ClinicController<TYPE>::Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                               \
    template void ClinicController<TYPE>::Update(CALLBACK_ &&callback, const Requester &&requester, std::string_view data, std::optional<uint64_t> id); \
    template void ClinicController<TYPE>::Delete(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id);                        \
    template void ClinicController<TYPE>::Search(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                             \
    template void ClinicController<TYPE>::GetVisits(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id);

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep
// Instantiate for all entity types

INSTANTIATE_CLINIC_CONTROLLER(Patient)
INSTANTIATE_CLINIC_CONTROLLER(Health)
INSTANTIATE_CLINIC_CONTROLLER(PatientDrugs)
INSTANTIATE_CLINIC_CONTROLLER(Reports)
INSTANTIATE_CLINIC_CONTROLLER(Visits)
INSTANTIATE_CLINIC_CONTROLLER(VisitDrugs)
INSTANTIATE_CLINIC_CONTROLLER(Prescriptions)
INSTANTIATE_CLINIC_CONTROLLER(Requests)
INSTANTIATE_CLINIC_CONTROLLER(PaidServices)
