#pragma once

#include <regex>
#include <string>
#include <vector>

class SqlInjectionDetector
{
   public:
    static void                                   initialize();
    [[nodiscard]] static bool                     isQuerySqlInjection(const std::string& query);
    // Checks the query text AND the bound parameter values: $1, $2, ... are replaced by the quoted values
    // in a throwaway copy that is used for detection only (never executed).
    [[nodiscard]] static bool                     isQuerySqlInjection(const std::string& query, const std::vector<std::string>& params);
    [[nodiscard]] static std::string              buildDetectionQuery(const std::string& query, const std::vector<std::string>& params);
    static void                                   addCustomPattern(const std::string& pattern);
    [[nodiscard]] static std::vector<std::string> getDetectedPatterns(const std::string& query);

   private:
    static bool               containsSuspiciousPattern(const std::string& query, std::vector<std::string>& detectedPatterns);
    [[nodiscard]] static bool hasUnbalancedQuotes(const std::string& query);
    [[nodiscard]] static bool hasCommentTokens(const std::string& query);
    [[nodiscard]] static bool hasMultipleQueries(const std::string& query);
    [[nodiscard]] static bool hasRiskyKeywords(const std::string& query);

    static std::vector<std::string> customPatterns;
    static std::vector<std::regex>  suspiciousPatterns;
    static std::vector<std::string> riskyKeywords;
};
