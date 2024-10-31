#pragma once

#include <crow.h>
#include <fmt/core.h>

#include <jsoncons/json.hpp>
#include <memory>
#include <type_traits>

#include "controllers/clientcontroller/clientcontrollerbase.hpp"
#include "controllers/databasecontroller/databasecontroller.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/base/client.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"
#include "utils/resthelper/resthelper.hpp"
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
            passwordCrypt  = Store::getObject<PasswordCrypt>();
            sessionManager = Store::getObject<SessionManager>();
        }
        catch (const std::exception& e)
        {
            std::cerr << fmt::format("Exception in ClientController constructor: {}\n", e.what());
            exit(EXIT_FAILURE);
        }
    }

    ~ClientController() override = default;

    void Create(const crow::request& req, crow::response& res, const json& request_json) final;
    void Read(const crow::request& req, crow::response& res, const json& request_json) final;
    void Update(const crow::request& req, crow::response& res, const json& request_json) final;
    void Delete(const crow::request& req, crow::response& res, const json& request_json) final;
    void Search(const crow::request& req, crow::response& res, const json& request_json) final;

    std::optional<uint64_t> Login(const crow::request& req, crow::response& res, const jsoncons::json& credentials) final;
    void                    Logout(const crow::request& req, crow::response& res, const std::optional<std::string>& token) final;
    void                    Suspend(const crow::request& req, crow::response& res, const json& criteria) final;
    void                    Activate(const crow::request& req, crow::response& res, const json& criteria) final;
    void                    ResetPassword(const crow::request& req, crow::response& res, const json& reset_json) final;
    void                    GetServices(const crow::request& req, crow::response& res, std::optional<uint64_t> client_id) final;

   private:
    std::shared_ptr<TokenManager>   tokenManager;
    std::shared_ptr<PasswordCrypt>  passwordCrypt;
    std::shared_ptr<SessionManager> sessionManager;
};

#include "controllers/clientcontroller/clientcontroller.tpp"