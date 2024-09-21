#include "utils/communicate/communicate.hpp"

#include <curl/curl.h>
#include <fmt/format.h>

#include <functional>
#include <iostream>
#include <map>
#include <string>

std::optional<std::string> Communicate::sendRequest(const std::string& server, const int port, const std::string& path,
                                                    const crow::HTTPMethod& method, const std::string& data)
{
    CURL*              curl    = nullptr;
    struct curl_slist* headers = NULL;
    headers                    = curl_slist_append(headers, "Content-Type: application/json");
    std::string response;

    try
    {
        curl = curl_easy_init();
        if (!curl)
        {
            throw std::runtime_error("Failed to initialize cURL.");
        }

        // Using fmt::format to construct the URL string
        std::string url = fmt::format("{}:{}{}", server, port, path);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Map of methods to their respective cURL options
        static const std::map<crow::HTTPMethod, std::function<void()>> methodMap = {
            {crow::HTTPMethod::POST,
             [&curl, &data]()
             {
                 curl_easy_setopt(curl, CURLOPT_POST, 1L);
                 curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
             }},
            {crow::HTTPMethod::GET, [&curl]() { curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L); }}};

        auto it = methodMap.find(method);
        if (it != methodMap.end())
        {
            it->second();  // Call the appropriate method
        }
        else
        {
            throw std::invalid_argument("Unsupported HTTP method: " + crow::method_name(method));
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
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        if (curl)
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
        std::cerr << "Exception occurred: " << e.what() << std::endl;
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
