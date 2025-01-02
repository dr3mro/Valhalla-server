#pragma once

#include <cstdint>
#include <string>

class SqlInjectionDetector
{
   public:
    static bool isSafeQuery(const std::string& query);

   private:
    static bool     containsHexEncoding(const std::string& query);
    static bool     containsUrlEncoding(const std::string& query);
    static bool     containsUnbalancedQuotes(const std::string& query);
    static bool     containsSuspiciousChars(const std::string& query);
    static uint32_t countOccurrences(const std::string& str, char _char);
    static void     logSecurityEvent(const std::string& message, const std::string& query);
};