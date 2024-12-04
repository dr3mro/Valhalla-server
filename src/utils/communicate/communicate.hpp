#pragma once

#include <curl/curl.h>
#include <drogon/drogon.h>

#include <optional>
#include <string>

class Communicate
{
   public:
    static std::optional<std::string> sendRequest(const std::string& server, int port, const std::string& path,
                                                  const drogon::HttpMethod& method, const std::string& data);

   private:
    static std::optional<std::string> handleResponse(CURL* curl);
    static std::optional<size_t>      WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response);
};
