#pragma once

#include <drogon/utils/FunctionTraits.h>

template <typename CALLBACK>
class ClinicControllerBase
{
   public:
    explicit ClinicControllerBase() = default;

    virtual ~ClinicControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK&& callback, std::string_view data)                             = 0;
    virtual void Read(CALLBACK&& callback, std::string_view data)                               = 0;
    virtual void Update(CALLBACK&& callback, std::string_view data, std::optional<uint64_t> id) = 0;
    virtual void Delete(CALLBACK&& callback, std::optional<uint64_t> id)                        = 0;
    virtual void Search(CALLBACK&& callback, std::string_view data)                             = 0;
    virtual void GetVisits(CALLBACK&& callback, std::optional<uint64_t> id)                     = 0;
};
