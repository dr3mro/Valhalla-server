#include "utils/communicate/communicate.hpp"

#include <curl/curl.h>
#include <fmt/format.h>

#include <string>

#include "utils/message/message.hpp"

std::optional<std::string> Communicate::sendRequest(
    const std::string& server, const int port, const std::string& path, const drogon::HttpMethod& method, const std::string& data)
{
    CURL*              curl    = nullptr;
    struct curl_slist* headers = nullptr;
    headers                    = curl_slist_append(headers, "Content-Type: application/json");
    std::string response;
    std::string url;

    try
    {
        curl = curl_easy_init();
        if (curl == nullptr)
        {
            throw std::runtime_error("Failed to initialize cURL.");
        }

        // Using fmt::format to construct the URL string
        url = fmt::format("{}:{}{}", server, port, path);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Map of methods to their respective cURL options
        static const std::map<drogon::HttpMethod, std::function<void()>> methodMap = {{drogon::HttpMethod::Post,
                                                                                          [&curl, &data]()
                                                                                          {
                                                                                              curl_easy_setopt(curl, CURLOPT_POST, 1L);
                                                                                              curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
                                                                                          }},
            {drogon::HttpMethod::Get, [&curl]() { curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L); }}};

        auto method_opt = methodMap.find(method);
        if (method_opt != methodMap.end())
        {
            method_opt->second();  // Call the appropriate method
        }
        else
        {
            throw std::invalid_argument("Unsupported HTTP method: " + drogon::to_string(method));
        }

        // Handle response and potential errors
        response = handleResponse(curl).value_or("");
        if (response.empty())
        {
            throw std::runtime_error("Failed to get a valid response.");
        }

        curl_easy_cleanup(curl);
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage(fmt::format("Failed to send request to {}.", url));
        Message::CriticalMessage(e.what());

        if (curl != nullptr)
        {
            curl_easy_cleanup(curl);
        }
        return std::nullopt;
    }

    return response;
}
std::optional<std::string> Communicate::handleResponse(CURL* curl)
{
    std::string response;

    try
    {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            throw std::runtime_error("cURL request failed: " + std::string(curl_easy_strerror(res)));
        }
    }
    catch (const std::exception& e)
    {
        Message::ErrorMessage(fmt::format("Failed to handle request."));
        Message::CriticalMessage(e.what());
        response.clear();  // Clear the response to indicate failure
    }

    return response;
}

std::optional<size_t> Communicate::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response)
{
    size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
