#pragma once

#include <string>
#include <unordered_map>

class Message
{
   public:
    enum class Level
    {
        Failure,
        Error,
        Warning,
        Success,
        Info,
        Debug
    };

    static const std::unordered_map<Level, std::string> color_map;

    Message()          = default;
    virtual ~Message() = default;

    static void FailureMessage(const std::string& status_message);
    static void ErrorMessage(const std::string& status_message);
    static void WarningMessage(const std::string& status_message);
    static void SuccessMessage(const std::string& status_message);
    static void InfoMessage(const std::string& status_message);
    static void DebugMessage(const std::string& status_message);

   private:
    static void MessageImpl(const std::string& status_message, Level level);
};
