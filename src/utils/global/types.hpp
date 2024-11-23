#pragma once
#include <string>
namespace api
{
    namespace v2
    {
        class Global
        {
           public:
            using HttpError = struct HttpError
            {
                int         code;
                std::string message;
            };
        };
    }  // namespace v2
}  // namespace api