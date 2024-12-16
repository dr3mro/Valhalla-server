#pragma once

#include <type_traits>
namespace api::v2
{
    class Client;
}

class Service;
class Case;
class Appointment;

template <typename T>
concept Client_t = std::is_base_of_v<api::v2::Client, T>;

template <typename T>
concept Service_t = std::is_base_of_v<Service, T>;

template <typename T>
concept Case_t = std::is_base_of_v<Case, T>;

template <typename T>
concept Appointment_t = std::is_base_of_v<Appointment, T>;