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
        std::regex(R"((--|#|\/\*|\*\/);?)", std::regex::icase),

        // Stacked queries
        std::regex(R"(;.*?([a-z]|$))", std::regex::icase),

        // Common SQL injection patterns
        std::regex(R"(\b(union\s+all|union\s+select|load_file|outfile|dumpfile)\b)", std::regex::icase),

        // Dangerous SQL commands
        std::regex(R"(\b(alter|create|delete|drop|exec(ute)?|insert|merge|select|update|upsert)\b\s*\(?)", std::regex::icase),

        // Common tautologies and boolean-based injections
        std::regex(
            R"(\b(is\s+null|is\s+not\s+null|like\s+\'%|or\s+\'\'=\'|\'\s+or\s+\'|\'\s+or\s+1=1|or\s+1=1|\'=\'|\'or\'\'=\'|1\s*=\s*1))", std::regex::icase),

        // Time-based and sleep-based attacks
        std::regex(R"(\b(benchmark|delay|sleep|waitfor)\b.*?\([^\)]*\))", std::regex::icase),

        // System command execution attempts
        std::regex(R"(\b(sys|system|user|database|current|contains|convert|version)\b\s*\()", std::regex::icase)};

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
