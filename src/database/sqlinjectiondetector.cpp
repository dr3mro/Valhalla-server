#include "database/sqlinjectiondetector.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#include "utils/message/message.hpp"

bool SqlInjectionDetector::isSafeQuery(const std::string& query)
{
    // Convert query to lowercase for case-insensitive matching
    std::string lowerQuery = query;
    std::ranges::transform(lowerQuery, lowerQuery.begin(), ::tolower);

    // More comprehensive SQL injection patterns
    static const std::vector<std::regex> sqlInjectionPatterns = {// Comment patterns
        // Malicious comment patterns with SQL commands
        std::regex(R"((--|\/*)\s*((select|union|delete|drop|insert|update)\s|$))", std::regex::icase),

        // Malicious stacked queries
        std::regex(R"(;\s*(select|union|delete|drop|insert|update)\s)", std::regex::icase),

        // Dangerous combinations
        std::regex(R"(union\s+all\s+select\s)", std::regex::icase),

        // SQL command injection attempts
        std::regex(R"(\b(drop|delete)\s+table\b)", std::regex::icase),

        // Common attack patterns with boundaries
        std::regex(R"(\'\s+or\s+\'1\'\s*=\s*\'1\'|\'\s+or\s+1\s*=\s*1\s+--)", std::regex::icase),

        // Time-based attacks with specific context
        std::regex(R"(waitfor\s+delay\s+\')", std::regex::icase),

        // System command execution with context
        std::regex(R"(exec(\s|\+)+(xp|sp)_)", std::regex::icase)};

    // Additional security checks
    if (containsHexEncoding(lowerQuery) || containsUrlEncoding(lowerQuery) || containsUnbalancedQuotes(query) || containsSuspiciousChars(query))
    {
        logSecurityEvent("Suspicious query detected", query);
        return false;
    }

    // Check against all patterns
    for (const auto& pattern : sqlInjectionPatterns)
    {
        if (std::regex_search(lowerQuery, pattern))
        {
            logSecurityEvent("SQL injection pattern matched", query);
            fmt::print("{}",;
            return false;
        }
    }

    // Check for batch query attempts
    if (countOccurrences(lowerQuery, ';') > 1)
    {
        logSecurityEvent("Multiple query execution attempt detected", query);
        return false;
    }

    return true;
}

bool SqlInjectionDetector::containsHexEncoding(const std::string& query)
{
    static const std::regex hexPattern(R"(0x[0-9a-f]+)", std::regex::icase);
    return std::regex_search(query, hexPattern);
}

bool SqlInjectionDetector::containsUrlEncoding(const std::string& query)
{
    static const std::regex urlPattern(R"(%[0-9a-f]{2})", std::regex::icase);
    return std::regex_search(query, urlPattern);
}

bool SqlInjectionDetector::containsUnbalancedQuotes(const std::string& query)
{
    int singleQuotes = 0;
    int doubleQuotes = 0;
    for (char _char : query)
    {
        if (_char == '\'')
        {
            singleQuotes++;
        }
        if (_char == '"')
        {
            doubleQuotes++;
        }
    }
    return (singleQuotes % 2 != 0) || (doubleQuotes % 2 != 0);
}

bool SqlInjectionDetector::containsSuspiciousChars(const std::string& query)
{
    static const std::string suspiciousChars = "\\<>~!$%^&*|";
    return query.find_first_of(suspiciousChars) != std::string::npos;
}

uint32_t SqlInjectionDetector::countOccurrences(const std::string& str, char _char)
{
    uint32_t count = std::count(str.begin(), str.end(), _char);
    return count;
}

void SqlInjectionDetector::logSecurityEvent(const std::string& message, const std::string& query)
{
    // Replace with your actual logging mechanism
    Message::InfoMessage(fmt::format("[Security Alert] {}: {}", message, query));
}
