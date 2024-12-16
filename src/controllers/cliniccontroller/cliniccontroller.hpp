#pragma once

#include <cstdint>

#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "utils/global/global.hpp"

template <typename T>
class ClinicController : public EntityController<T>, public ClinicControllerBase
{
   private:
    std::shared_ptr<GateKeeper> gateKeeper = Store::getObject<GateKeeper>();

    template <typename U = T>
    void CreateImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
        requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value);

    template <typename U = T>
    void CreateImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
        requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value);

    template <typename U = T>
    void DeleteImpl(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id)
        requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value);

    template <typename U = T>
    void DeleteImpl(CALLBACK_ &&callback, [[maybe_unused]] const Requester &&requester, std::optional<uint64_t> id)
        requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value);

    template <typename U = T>
    void SearchImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
        requires(!std::is_same<U, Patient>::value);

    template <typename U = T>
    void SearchImpl(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
        requires(std::is_same<U, Patient>::value);

    template <typename U = T>
    void GetVisitsImpl(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> patient_id)
        requires(std::is_same<U, Patient>::value);

    template <typename U = T>
    void GetVisitsImpl(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> patient_id)
        requires(!std::is_same<U, Patient>::value);

   public:
    explicit ClinicController()          = default;
    virtual ~ClinicController() override = default;

    void Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) final;
    void Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) final;
    void Update(CALLBACK_ &&callback, const Requester &&requester, std::string_view data, std::optional<uint64_t> id) final;
    void Delete(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id) final;
    void Search(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) final;
    void GetVisits(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> patient_id) final;
};