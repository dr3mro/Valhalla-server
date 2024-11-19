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
