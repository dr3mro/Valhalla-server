template <typename T>
void AppointmentController<T>::Create(const crow::request &req, crow::response &res, const jsoncons::json &request_json)
{
    EntityController<T>::Create(req, res, request_json);
}

template <typename T>
void AppointmentController<T>::Read(const crow::request &req, crow::response &res, const json &request_json)
{
    EntityController<T>::Read(req, res, request_json);
}

template <typename T>
void AppointmentController<T>::Update(const crow::request &req, crow::response &res, const jsoncons::json &request_json)
{
    EntityController<T>::Update(req, res, request_json);
}

template <typename T>
void AppointmentController<T>::Delete(const crow::request &req, crow::response &res, const jsoncons::json &request_json)
{
    EntityController<T>::Delete(req, res, request_json);
}

template <typename T>
void AppointmentController<T>::Search(const crow::request &req, crow::response &res, const jsoncons::json &request_json)
{
    EntityController<T>::Search(req, res, request_json);
}
