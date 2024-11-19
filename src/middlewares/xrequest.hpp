// #pragma once

// #include <crow.h>

// #include <jsoncons/json.hpp>

// #include "utils/resthelper/resthelper.hpp"

// struct XRequest : crow::ILocalMiddleware
// {
//    private:
//     const std::string xRequestHeader = "X-Request";

//    public:
//     struct context
//     {
//         jsoncons::json criteria;
//     };

//     XRequest()          = default;
//     virtual ~XRequest() = default;

//     void before_handle(crow::request &req, crow::response &res, context &ctx)
//     {
//         if (!(req.headers.contains(xRequestHeader) && !req.get_header_value(xRequestHeader).empty()))
//         {
//             RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "Missing proper X-Request in header.");
//             return;
//         }
//         try
//         {
//             std::string                decoded;
//             std::optional<std::string> encoded = req.get_header_value(xRequestHeader);

//             if (!encoded)
//             {
//                 RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "X-Request header not provided.");
//                 return;
//             }

//             jsoncons::decode_base64(encoded.value().begin(), encoded.value().end(), decoded);

//             ctx.criteria = jsoncons::json::parse(decoded);
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
// };
