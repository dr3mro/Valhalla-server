#pragma once

#include <trantor/utils/Logger.h>

#include <string>
class Message
{
   public:
    Message()          = default;
    virtual ~Message() = default;
    // implemnet copy and move constructor
    Message(const Message& other)            = default;
    Message(Message&& other)                 = default;
    Message& operator=(const Message& other) = default;
    Message& operator=(Message&& other)      = default;

    static void        DebugMessage(const std::string& status_message);
    static void        InfoMessage(const std::string& status_message);
    static void        WarningMessage(const std::string& status_message);
    static void        ErrorMessage(const std::string& status_message);
    static void        CriticalMessage(const std::string& status_message);
    static void        ConfMessage(const std::string& status_message);
    static void        InitMessage(const std::string& status_message);
    static std::string get_color(trantor::Logger::LogLevel level);

   private:
    static void        MessageImpl(const std::string& _status_message, trantor::Logger::LogLevel _level);
    static void        MessageImpl(const std::string& _status_message, const std::string& _type);
    static std::string removeNewLine(const std::string& _str);
};
