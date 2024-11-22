
#pragma once

#include <cstdint>
#include <string_view>
template <typename CALLBACK>
class ServiceControllerBase
{
   public:
    explicit ServiceControllerBase() = default;

    virtual ~ServiceControllerBase() = default;

    // CRUDS
    virtual void Create(CALLBACK&& callback, std::string_view data)      = 0;
    virtual void Read(CALLBACK&& callback, std::string_view data)        = 0;
    virtual void Update(CALLBACK&& callback, std::string_view data)      = 0;
    virtual void Delete(CALLBACK&& callback, std::optional<uint64_t> id) = 0;
    virtual void Search(CALLBACK&& callback, std::string_view data)      = 0;
};
