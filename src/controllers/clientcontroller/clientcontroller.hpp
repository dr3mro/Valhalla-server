#pragma once

#include <drogon/drogon.h>
#include <fmt/core.h>

#include <jsoncons/json.hpp>
#include <memory>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/base/client.hpp"
#include "utils/message/message.hpp"
#include "utils/sessionmanager/sessionmanager.hpp"
#include "utils/tokenmanager/tokenmanager.hpp"

template <typename T>
concept Client_t = std::is_base_of_v<Client, T>;

template <Client_t T>

class ClientController : public EntityController<T>, public ClientControllerBase
{
   public:
    ClientController()
    {
        try
        {
            tokenManager   = Store::getObject<TokenManager>();
            sessionManager = Store::getObject<SessionManager>();
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Exception in ClientController constructor."));
            Message::CriticalMessage(e.what());
            exit(EXIT_FAILURE);
        }
    }

    virtual ~ClientController() final = default;

    void Create(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) final;
    void Read(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) final;
    void Update(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) final;
    void Delete(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::unordered_map<std::string, std::string>& params) final;
    void Search(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) final;
    std::optional<uint64_t> Login(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) final;
    void                    Logout(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::optional<std::string>& token) final;
    void Suspend(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::unordered_map<std::string, std::string>& params) final;
    void Activate(std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::unordered_map<std::string, std::string>& params) final;
    void ResetPassword(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string_view data) final;
    void GetServices(std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::optional<uint64_t> client_id) final;

   private:
    std::shared_ptr<TokenManager>   tokenManager;
    std::shared_ptr<SessionManager> sessionManager;
};

#include "controllers/clientcontroller/clientcontroller.tpp"
