
#include "configurator.hpp"
// Specialization for DODDetectorConfig
template <>
Configurator::DOSDetectorConfig& Configurator::get<Configurator::DOSDetectorConfig>()
{
    return dos_detector_config_;
}

// Specialization for DatabaseConfig
template <>
Configurator::DatabaseConfig& Configurator::get<Configurator::DatabaseConfig>()
{
    return database_config_;
}

// Specialization for ServerConfig
template <>
Configurator::ServerConfig& Configurator::get<Configurator::ServerConfig>()
{
    return server_config_;
}
// Specialization for TokenManagerParameters
template <>
Configurator::TokenManagerParameters& Configurator::get<Configurator::TokenManagerParameters>()
{
    return token_manager_parameters_;
}

template <>
Configurator::FrontEndConfig& Configurator::get<Configurator::FrontEndConfig>()
{
    return frontend_config_;
}

template <>
Configurator::EmailSenderConfig& Configurator::get<Configurator::EmailSenderConfig>()
{
    return email_sender_config_;
}