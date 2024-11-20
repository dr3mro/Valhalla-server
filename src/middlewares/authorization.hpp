#include <drogon/drogon.h>

#include <iostream>

class AuthorizationMiddleware : public drogon::HttpMiddleware<AuthorizationMiddleware>
{
   public:
    AuthorizationMiddleware()          = default;
    virtual ~AuthorizationMiddleware() = default;

    // This method is invoked for each incoming request
    void handle(drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp, const drogon::HttpFiltersCallback& callback) override
    {
        // Check for Authorization header
        auto authHeader = req->getHeader("Authorization");

        if (authHeader.empty())
        {
            // If no Authorization header is provided, return 401 Unauthorized
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::HttpStatusCode::k401Unauthorized);
            response->setBody("Missing Authorization token");
            callback(response);
            return;
        }

        // Validate the token (here, we use a simple check for demo purposes)
        if (!isValidToken(authHeader))
        {
            // If the token is invalid, return 403 Forbidden
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::HttpStatusCode::k403Forbidden);
            response->setBody("Invalid or expired token");
            callback(response);
            return;
        }

        // If the token is valid, continue processing the request
        callback(req);
    }

   private:
    // Token validation logic (this is just a stub for the demo)
    bool isValidToken(const std::string& token)
    {
        // In a real implementation, you would check the token against a database
        // or an external authorization server. Here we just check if the token is "valid-token".
        return token == "valid-token";
    }
};

// #pragma once
// #include <crow.h>

// #include <jsoncons/json.hpp>

// #include "store/store.hpp"
// #include "utils/resthelper/resthelper.hpp"
// #include "utils/tokenmanager/tokenmanager.hpp"

// struct Authorization : crow::ILocalMiddleware
// {
//     struct context
//     {
//         TokenManager::LoggedUserInfo userInfo;
//     };
//     Authorization() { tokenManager = Store::getObject<TokenManager>(); }

//     virtual ~Authorization() = default;

//     void before_handle(crow::request &req, crow::response &res, context &ctx)
//     {
//         try
//         {
//             std::optional<std::string> authorization = req.get_header_value("Authorization");

//             if (!authorization)
//             {
//                 RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Authorization Header not found");
//                 return;
//             }

//             if (authorization.value().substr(0, 7) == "Bearer ")
//             {
//                 ctx.userInfo.token = authorization.value().substr(7);
//             }

//             if (!ctx.userInfo.token)
//             {
//                 RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Authorization token not found");
//                 return;
//             }

//             if (!tokenManager->ValidateToken(ctx.userInfo))
//             {
//                 RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Authorization Denied");
//                 return;
//             }
//             return;
//         }
//         catch (const std::exception &e)
//         {
//             RestHelper::failureResponse(res, e.what());
//             return;
//         }
//     }
//     void after_handle(crow::request &req, crow::response &res, context &ctx)
//     {
//         (void)req;
//         (void)res;
//         (void)ctx;
//     }

//    private:
//     std::shared_ptr<TokenManager> tokenManager;
// };
