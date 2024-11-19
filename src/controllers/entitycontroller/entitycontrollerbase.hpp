
#pragma once

#include <drogon/drogon.h>
class EntityControllerBase
{
   public:
    explicit EntityControllerBase() = default;

    virtual ~EntityControllerBase() = default;

    // CRUDS
    virtual void Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) = 0;
    virtual void Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data)   = 0;
    virtual void Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) = 0;
    virtual void Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::unordered_map<std::string, std::string>&   params)                            = 0;
    virtual void Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) = 0;
};