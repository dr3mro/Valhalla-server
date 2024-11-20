#pragma once

#include <drogon/utils/FunctionTraits.h>

class ClinicControllerBase
{
   public:
    explicit ClinicControllerBase() = default;

    virtual ~ClinicControllerBase() = default;

    // CRUDS
    virtual void Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)         = 0;
    virtual void Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)           = 0;
    virtual void Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)         = 0;
    virtual void Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> id)    = 0;
    virtual void Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)         = 0;
    virtual void GetVisits(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> id) = 0;
};
