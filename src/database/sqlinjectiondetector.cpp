#include "database/sqlinjectiondetector.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <regex>
#include <string>
#include <vector>

// Define static members
std::vector<std::string> SqlInjectionDetector::customPatterns;
std::vector<std::regex>  SqlInjectionDetector::suspiciousPatterns;
std::vector<std::string> SqlInjectionDetector::riskyKeywords;

void SqlInjectionDetector::initialize()
{
    // Initialize common SQL injection patterns
    suspiciousPatterns = {std::regex(R"('\s*OR\s+'1'\s*=\s*'1)", std::regex_constants::icase), std::regex(R"('\s*OR\s+1\s*=\s*1)", std::regex_constants::icase),
        std::regex(R"('\s*OR\s+'a'\s*=\s*'a)", std::regex_constants::icase), std::regex("--\\s*$"),
        std::regex(";\\s*DROP\\s+TABLE", std::regex_constants::icase), std::regex("UNION\\s+ALL\\s+SELECT", std::regex_constants::icase),
        std::regex("UNION\\s+SELECT", std::regex_constants::icase), std::regex("INTO\\s+OUTFILE", std::regex_constants::icase),
        std::regex("LOAD_FILE", std::regex_constants::icase)};

    // Initialize risky keywords
    riskyKeywords = {"EXEC", "EXECUTE", "SLEEP", "DELAY", "BENCHMARK", "WAITFOR", "XP_CMDSHELL", "SYSTEM", "SHUTDOWN"};
}

bool SqlInjectionDetector::isSafeQuery(const std::string& query)
{
    std::vector<std::string> detectedPatterns;
    return !(containsSuspiciousPattern(query, detectedPatterns) || hasUnbalancedQuotes(query) || hasCommentTokens(query) || hasMultipleQueries(query) ||
             hasRiskyKeywords(query));
}

void SqlInjectionDetector::addCustomPattern(const std::string& pattern)
{
    customPatterns.push_back(pattern);
    suspiciousPatterns.emplace_back(pattern, std::regex_constants::icase);
}

std::vector<std::string> SqlInjectionDetector::getDetectedPatterns(const std::string& query)
{
    std::vector<std::string> detectedPatterns;
    containsSuspiciousPattern(query, detectedPatterns);

    if (hasUnbalancedQuotes(query))
    {
        detectedPatterns.emplace_back("Unbalanced quotes detected");
    }
    if (hasCommentTokens(query))
    {
        detectedPatterns.emplace_back("SQL comment tokens detected");
    }
    if (hasMultipleQueries(query))
    {
        detectedPatterns.emplace_back("Multiple queries detected");
    }
    if (hasRiskyKeywords(query))
    {
        detectedPatterns.emplace_back("Risky SQL keywords detected");
    }

    return detectedPatterns;
}

bool SqlInjectionDetector::containsSuspiciousPattern(const std::string& query, std::vector<std::string>& detectedPatterns)
{
    bool found = false;

    for (size_t i = 0; i < suspiciousPatterns.size(); ++i)
    {
        if (std::regex_search(query, suspiciousPatterns[i]))
        {
            found = true;
            if (i < customPatterns.size())
            {
                detectedPatterns.push_back("Custom pattern matched: " + customPatterns[i]);
            }
            else
            {
                detectedPatterns.emplace_back("Built-in suspicious pattern detected");
            }
        }
    }

    return found;
}

bool SqlInjectionDetector::hasUnbalancedQuotes(const std::string& query)
{
    int  singleQuotes  = 0;
    int  doubleQuotes  = 0;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (size_t i = 0; i < query.length(); ++i)
    {
        if (query[i] == '\'' && !inDoubleQuote)
        {
            if (i == 0 || query[i - 1] != '\\')
            {
                singleQuotes++;
                inSingleQuote = !inSingleQuote;
            }
        }
        else if (query[i] == '"' && !inSingleQuote)
        {
            if (i == 0 || query[i - 1] != '\\')
            {
                doubleQuotes++;
                inDoubleQuote = !inDoubleQuote;
            }
        }
    }

    return (singleQuotes % 2 != 0) || (doubleQuotes % 2 != 0);
}

bool SqlInjectionDetector::hasCommentTokens(const std::string& query)
{
    return query.find("--") != std::string::npos || query.find("/*") != std::string::npos || query.find('#') != std::string::npos;
}

bool SqlInjectionDetector::hasMultipleQueries(const std::string& query)
{
    bool inString   = false;
    char stringChar = 0;

    for (size_t i = 0; i < query.length(); ++i)
    {
        if (query[i] == '\'' || query[i] == '"')
        {
            if (!inString)
            {
                inString   = true;
                stringChar = query[i];
            }
            else if (query[i] == stringChar)
            {
                if (i == 0 || query[i - 1] != '\\')
                {
                    inString = false;
                }
            }
        }
        else if (query[i] == ';' && !inString)
        {
            // Check if there's any non-whitespace after the semicolon
            for (size_t j = i + 1; j < query.length(); ++j)
            {
                if (std::isspace(query[j]) == 0)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool SqlInjectionDetector::hasRiskyKeywords(const std::string& query)
{
    std::string upperQuery = query;
    std::ranges::transform(upperQuery, upperQuery.begin(), ::toupper);

    for (const auto& keyword : riskyKeywords)
    {
        size_t pos = upperQuery.find(keyword);
        if (pos != std::string::npos)
        {
            // Check if the keyword is a complete word
            bool isWordStart = (pos == 0 || (std::isalnum(upperQuery[pos - 1]) == 0));
            bool isWordEnd   = (pos + keyword.length() == upperQuery.length() || (std::isalnum(upperQuery[pos + keyword.length()]) == 0));

            if (isWordStart && isWordEnd)
            {
                return true;
            }
        }
    }

    return false;
}
