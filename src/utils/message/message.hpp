#pragma once

#include <string>
#include <unordered_map>

#include "crow/logging.h"

class Message
{
   public:
    static const std::unordered_map<crow::LogLevel, std::string> color_map;

    Message()          = default;
    virtual ~Message() = default;
    static void DebugMessage(const std::string& status_message);
    static void InfoMessage(const std::string& status_message);
    static void WarningMessage(const std::string& status_message);
    static void ErrorMessage(const std::string& status_message);
    static void CriticalMessage(const std::string& status_message);
    static void ConfMessage(const std::string& status_message);
    static void InitMessage(const std::string& status_message);

   private:
    static void MessageImpl(const std::string& status_message, crow::LogLevel level);
    static void MessageImpl(const std::string& status_messageconst, const std::string& type);
};
