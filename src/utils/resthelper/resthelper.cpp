#include "resthelper.hpp"

#include "fmt/core.h"

void RestHelper::failureResponse(crow::response& res, const std::string& status_message)
{
    res.code = crow::status::INTERNAL_SERVER_ERROR;
    res.add_header("msg", "fail");
    res.end(fmt::format("Error: {}", status_message));
}

void RestHelper::errorResponse(crow::response& res, const crow::status& status, const std::string& status_message)
{
    res.code = status;
    res.add_header("msg", "fail");
    res.end(fmt::format("Error: {}", status_message));
}

void RestHelper::successResponse(crow::response& res, const crow::status& status, const std::string& result)
{
    res.code = status;
    res.add_header("msg", "success");
    res.end(result);
}