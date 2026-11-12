#include "gatekeeper/sqlinjectiondetectoor/sqlinjectiondetector.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <regex>
#include <string>
#include <vector>

#include "utils/message/message.hpp"

// Define static members
std::vector<std::string> SqlInjectionDetector::customPatterns;
std::vector<std::regex>  SqlInjectionDetector::suspiciousPatterns;
std::vector<std::string> SqlInjectionDetector::riskyKeywords;

void SqlInjectionDetector::initialize()
{
    // Initialize common SQL injection patterns
    suspiciousPatterns = {
        // Existing patterns
        std::regex(R"('\s*OR\s+'1'\s*=\s*'1)", std::regex_constants::icase), std::regex(R"('\s*OR\s+1\s*=\s*1)", std::regex_constants::icase),
        std::regex(R"('\s*OR\s+'a'\s*=\s*'a)", std::regex_constants::icase), std::regex("--\\s*$"),
        std::regex(";\\s*DROP\\s+TABLE", std::regex_constants::icase),
        // UNION patterns disabled: the app's own queries (e.g. provider/user services) legitimately use UNION ALL SELECT.
        // std::regex("UNION\\s+ALL\\s+SELECT", std::regex_constants::icase),
        // std::regex("UNION\\s+SELECT", std::regex_constants::icase),
        std::regex("INTO\\s+OUTFILE", std::regex_constants::icase), std::regex("LOAD_FILE", std::regex_constants::icase),

        // Additional patterns
        std::regex("';\\s*--", std::regex_constants::icase),                             // Single quote followed by comment
        std::regex("';\\s*DROP\\s+TABLE", std::regex_constants::icase),                  // Drop table attempt
        std::regex("';\\s*INSERT\\s+INTO", std::regex_constants::icase),                 // Insert into statement
        std::regex("';\\s*SELECT\\s+FROM", std::regex_constants::icase),                 // Select from injection
        std::regex("';\\s*UPDATE\\s+SET", std::regex_constants::icase),                  // Update table query
        std::regex("';\\s*DELETE\\s+FROM", std::regex_constants::icase),                 // Delete from query
        std::regex(R"(';\s*AND\s*1\s*=\s*1)", std::regex_constants::icase),              // AND 1=1 condition
        std::regex(R"(';\s*AND\s*1\s*<>\s*0)", std::regex_constants::icase),             // AND 1<>0 condition
        std::regex(R"(';\s*OR\s*1\s*<>\s*0)", std::regex_constants::icase),              // OR 1<>0 condition
        std::regex("';\\s*EXEC", std::regex_constants::icase),                           // EXEC command
        std::regex("';\\s*XP_CMDSHELL", std::regex_constants::icase),                    // SQL Server's xp_cmdshell
        std::regex("';\\s*WAITFOR", std::regex_constants::icase),                        // Waitfor delay
        std::regex("';\\s*BENCHMARK", std::regex_constants::icase),                      // Benchmark function (MySQL)
        std::regex("';\\s*GROUP\\s+BY", std::regex_constants::icase),                    // GROUP BY clause used in injection
        std::regex("';\\s*HAVING", std::regex_constants::icase),                         // HAVING clause used in injection
        std::regex("';\\s*NULL\\s+UNION", std::regex_constants::icase),                  // UNION SELECT NULL
        std::regex("';\\s*ORDER\\s+BY", std::regex_constants::icase),                    // ORDER BY clause injection
        std::regex("';\\s*REVOKE\\s+ALL", std::regex_constants::icase),                  // REVOKE command
        std::regex("';\\s*TRUNCATE\\s+TABLE", std::regex_constants::icase),              // TRUNCATE TABLE command
        std::regex(R"(';\s*LOAD\s+DATA\s+INFILE)", std::regex_constants::icase),         // LOAD DATA INFILE command (MySQL)
        std::regex("';\\s*--", std::regex_constants::icase),                             // Double hyphen as comment syntax
        std::regex("SELECT.*FROM\\s+INFORMATION_SCHEMA", std::regex_constants::icase),   // Information schema query
        std::regex(R"(SELECT\s+.*\s+FROM\s+mysql\.user)", std::regex_constants::icase),  // MySQL user table
        std::regex("';\\s*SHUTDOWN", std::regex_constants::icase),                       // Database shutdown attempt
        std::regex(R"(';\s*XOR\s+1\s*=\s*1)", std::regex_constants::icase)               // XOR condition attack
    };

    // Initialize risky keywords
    riskyKeywords = {"EXEC", "EXECUTE", "SLEEP", "DELAY", "BENCHMARK", "WAITFOR", "XP_CMDSHELL", "SYSTEM", "SHUTDOWN"};
}

bool SqlInjectionDetector::isQuerySqlInjection(const std::string& query)
{
    std::vector<std::string> detectedPatterns;
    bool                     isSqlInjection = (containsSuspiciousPattern(query, detectedPatterns) || hasUnbalancedQuotes(query) || hasCommentTokens(query) ||
                           hasMultipleQueries(query) || hasRiskyKeywords(query));

    if (isSqlInjection)
    {
        Message::WarningMessage("A Sql Injection pattern is detected in generated query.");
        Message::WarningMessage(query);

        // Print the reasons for SQL injection detection
        if (!detectedPatterns.empty())
        {
            Message::WarningMessage("Detected patterns: ");
            for (const auto& pattern : detectedPatterns)
            {
                Message::WarningMessage(pattern);
            }
        }
        else
        {
            Message::WarningMessage("No specific patterns were matched, but other suspicious conditions were detected.");
        }
    }

    return isSqlInjection;
}

std::string SqlInjectionDetector::buildDetectionQuery(const std::string& query, const std::vector<std::string>& params)
{
    std::string out;
    out.reserve(query.size() + params.size() * 16);

    for (size_t i = 0; i < query.size(); ++i)
    {
        if (query[i] == '$' && i + 1 < query.size() && (std::isdigit(static_cast<unsigned char>(query[i + 1])) != 0))
        {
            size_t j   = i + 1;
            size_t idx = 0;
            while (j < query.size() && (std::isdigit(static_cast<unsigned char>(query[j])) != 0))
            {
                idx = idx * 10 + static_cast<size_t>(query[j] - '0');
                ++j;
            }

            if (idx >= 1 && idx <= params.size())
            {
                // Quote it the way an inlined SQL literal would look (' doubled), so a payload such as
                // "' OR 1=1 --" shows up in the string exactly as it would if it had been concatenated.
                out += '\'';
                for (const char ch : params[idx - 1])
                {
                    out += ch;
                    if (ch == '\'')
                    {
                        out += '\'';
                    }
                }
                out += '\'';
                i = j - 1;
                continue;
            }
        }
        out += query[i];
    }
    return out;
}

bool SqlInjectionDetector::isQuerySqlInjection(const std::string& query, const std::vector<std::string>& params)
{
    return isQuerySqlInjection(buildDetectionQuery(query, params));
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

    // '_' counts as part of a word, so identifiers such as sleep_apnea or system_id are not keyword hits.
    const auto isWordChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };

    for (const auto& keyword : riskyKeywords)
    {
        // Look at every occurrence, not just the first one.
        for (size_t pos = upperQuery.find(keyword); pos != std::string::npos; pos = upperQuery.find(keyword, pos + 1))
        {
            const bool isWordStart = (pos == 0 || !isWordChar(upperQuery[pos - 1]));
            const bool isWordEnd   = (pos + keyword.length() == upperQuery.length() || !isWordChar(upperQuery[pos + keyword.length()]));

            if (isWordStart && isWordEnd)
            {
                return true;
            }
        }
    }

    return false;
}
