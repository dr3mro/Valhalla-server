

template <typename T>
void ServiceController<T>::Create(std::function<void(const drogon::HttpResponsePtr &)> &callback, std::string_view data)
{
    EntityController<T>::Create(callback, data);
}
// template <typename T>
// void ServiceController<T>::Read(const crow::request &req, crow::response &res, const json &request_json)
// {
//     EntityController<T>::Read(req, res, request_json);
// }

// template <typename T>
// void ServiceController<T>::Update(const crow::request &req, crow::response &res, const json &request_json)
// {
//     EntityController<T>::Update(req, res, request_json);
// }

// template <typename T>
// void ServiceController<T>::Delete(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params)
// {
//     EntityController<T>::Delete(req, res, params);
// }

// template <typename T>
// void ServiceController<T>::Search(const crow::request &req, crow::response &res, const json &request_json)
// {
//     EntityController<T>::Search(req, res, request_json);
// }
