#pragma once

#include <drogon/utils/FunctionTraits.h>

#include "utils/global/callback.hpp"
class ClinicControllerBase
{
   public:
    explicit ClinicControllerBase() = default;

    virtual ~ClinicControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK_&& callback, std::string_view data)                             = 0;
    virtual void Read(CALLBACK_&& callback, std::string_view data)                               = 0;
    virtual void Update(CALLBACK_&& callback, std::string_view data, std::optional<uint64_t> id) = 0;
    virtual void Delete(CALLBACK_&& callback, std::optional<uint64_t> id)                        = 0;
    virtual void Search(CALLBACK_&& callback, std::string_view data)                             = 0;
    virtual void GetVisits(CALLBACK_&& callback, std::optional<uint64_t> id)                     = 0;
};
