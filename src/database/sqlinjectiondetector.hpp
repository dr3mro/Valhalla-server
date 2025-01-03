#pragma once

#include <regex>
#include <string>
#include <vector>

class SqlInjectionDetector
{
   public:
    static void                                   initialize();
    [[nodiscard]] static bool                     isSafeQuery(const std::string& query);
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
