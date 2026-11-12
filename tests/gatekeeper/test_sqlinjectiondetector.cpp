#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include <string>
#include <vector>

#include "gatekeeper/sqlinjectiondetectoor/sqlinjectiondetector.hpp"

using SqlInjectionDetector = ::SqlInjectionDetector;

namespace
{
    /// Malicious inputs detected by the *current* heuristics (unbalanced
    /// quotes, comment tokens, stacked queries, risky keywords).
    const std::vector<std::string> detectedMaliciousInputs = {
        "abc' OR 1=1",                                          // tautology with unbalanced quote
        "'; DROP TABLE users; --",                              // stacked query + comment
        "admin'--",                                             // quote + comment
        "1' OR '1'='1' --",                                     // classic tautology, unbalanced
        "SELECT * FROM users WHERE id = 1; DELETE FROM users",  // stacked queries
        "1; DELETE FROM users",                                 // stacked query
        "name'; INSERT INTO logs VALUES ('x')",                 // unbalanced quote + stacked
        "SLEEP(10)",                                            // risky keyword
        "sleep(5)",                                             // risky keyword, lower case
        "pg_sleep(10)",                                         // risky keyword (word boundary is '_')
        "WAITFOR DELAY '0:0:10'",                               // risky keyword
        "EXEC sp_who2",                                         // risky keyword
        "EXECUTE IMMEDIATE 'x'",                                // risky keyword
        "xp_cmdshell 'dir'",                                    // risky keyword
        "BENCHMARK(100000, MD5('x'))",                          // risky keyword
        "SHUTDOWN",                                             // risky keyword
        "id = 1 -- evil",                                       // comment token
        "id = 1 #evil",                                         // comment token
        "id = 1 /*evil*/",                                      // comment token
    };

    /// Benign inputs the current implementation must NOT flag.
    const std::vector<std::string> benignInputs = {
        "",
        "john_doe",
        "P0001",
        "The patient reports mild fever and fatigue",
        "select",                       // the bare word is not risky on its own
        "union",                        // ditto
        "SELECT * FROM users",          // plain read query, no comment/stacking
        "doctor UNION nurse",           // 'union' in prose
        "100% reliable",
        "café visit at 10:00",
        "name='O''Brien'",              // doubled/escaped quote - balanced
        std::string(512, 'a'),
        "0.5 ml of solution B every 6 hours",
    };

    /// Inputs that SHOULD be detected but are not, purely because
    /// initialize() never installs the built-in regexes (local variable
    /// shadows the static member - see tests/README.md, known bug 1).
    /// Ideal behaviour, documented with [!shouldfail].
    const std::vector<std::string> knownBugUndetectedInputs = {
        "' OR '1'='1",                              // no unbalanced quote (4 quotes)
        "1 UNION SELECT password FROM users",       // no quotes/comments/stacking
        "SELECT * FROM information_schema.tables",  // schema probing
        "1 UNION ALL SELECT id FROM mysql.user",    // mysql probing
        "SELECT * FROM users INTO OUTFILE '/tmp/x'  // outfile",
    };

    /// Benign inputs that the current implementation DOES flag (false
    /// positives). Documented with [!shouldfail]; see tests/README.md.
    const std::vector<std::string> knownBugFalsePositives = {
        "O'Brien",                                 // unbalanced-quote heuristic
        "The immune system reacts strongly",       // 'SYSTEM' risky keyword
        "Please delay the procedure by one hour",  // 'DELAY' risky keyword
        "Dr. Brown -- cardiac ward",               // '--' comment heuristic
        "#ward7 roster",                           // '#' comment heuristic
        "clinic visit -- checkup",                 // '--' comment heuristic
        "DOB 1990-12-31; note: none",              // every ';' is a "stacked query"
    };
}  // namespace

TEST_CASE("SqlInjectionDetector flags malicious queries caught by the active heuristics", "[gatekeeper][sqli]")
{
    SqlInjectionDetector::initialize();

    const auto &query = GENERATE(from_range(detectedMaliciousInputs));
    CAPTURE(query);
    REQUIRE(SqlInjectionDetector::isQuerySqlInjection(query));
}

TEST_CASE("SqlInjectionDetector does not flag benign inputs", "[gatekeeper][sqli]")
{
    SqlInjectionDetector::initialize();

    const auto &query = GENERATE(from_range(benignInputs));
    CAPTURE(query);
    REQUIRE_FALSE(SqlInjectionDetector::isQuerySqlInjection(query));
}

TEST_CASE("SqlInjectionDetector would flag classic injection signatures if the built-in patterns were installed", "[gatekeeper][sqli][.known-bug][!shouldfail]")
{
    // Known bug: initialize() builds its pattern list into a *local*
    // `suspiciousPatterns` vector, shadowing the static member, so the
    // built-in regexes are never active. These inputs are only caught by
    // chance (quotes/comments/keywords), not by the pattern table.
    // Fix: assign the vector to the static member in initialize().
    SqlInjectionDetector::initialize();

    const auto &query = GENERATE(from_range(knownBugUndetectedInputs));
    CAPTURE(query);
    REQUIRE(SqlInjectionDetector::isQuerySqlInjection(query));
}

TEST_CASE("SqlInjectionDetector would accept ordinary prose if the keyword/comment heuristics were tightened", "[gatekeeper][sqli][.known-bug][!shouldfail]")
{
    // Known bug: every '--', '/*', '#' and the whole words SYSTEM, DELAY,
    // EXEC, EXECUTE, ... anywhere in a query are treated as injection
    // signals, which rejects harmless medical text and dashes.
    SqlInjectionDetector::initialize();

    const auto &query = GENERATE(from_range(knownBugFalsePositives));
    CAPTURE(query);
    REQUIRE_FALSE(SqlInjectionDetector::isQuerySqlInjection(query));
}

TEST_CASE("SqlInjectionDetector reports the reason for a detection", "[gatekeeper][sqli]")
{
    SqlInjectionDetector::initialize();

    SECTION("comment tokens are reported")
    {
        const auto patterns = SqlInjectionDetector::getDetectedPatterns("1 -- trailing comment");
        REQUIRE_THAT(patterns, Catch::Matchers::Contains(std::string("SQL comment tokens detected")));
    }

    SECTION("multiple queries are reported")
    {
        const auto patterns = SqlInjectionDetector::getDetectedPatterns("1; DROP TABLE users");
        REQUIRE_THAT(patterns, Catch::Matchers::Contains(std::string("Multiple queries detected")));
    }

    SECTION("unbalanced quotes are reported")
    {
        const auto patterns = SqlInjectionDetector::getDetectedPatterns("O'Brien");
        REQUIRE_THAT(patterns, Catch::Matchers::Contains(std::string("Unbalanced quotes detected")));
    }

    SECTION("risky keywords are reported")
    {
        const auto patterns = SqlInjectionDetector::getDetectedPatterns("wait 5 then SLEEP(5)");
        REQUIRE_THAT(patterns, Catch::Matchers::Contains(std::string("Risky SQL keywords detected")));
    }

    SECTION("clean input reports nothing")
    {
        const auto patterns = SqlInjectionDetector::getDetectedPatterns("a plain phrase");
        REQUIRE(patterns.empty());
    }
}

TEST_CASE("SqlInjectionDetector honours custom patterns", "[gatekeeper][sqli]")
{
    SqlInjectionDetector::initialize();
    SqlInjectionDetector::addCustomPattern("MAGIC_EVIL_PATTERN");

    SECTION("a matching query is flagged")
    {
        REQUIRE(SqlInjectionDetector::isQuerySqlInjection("field = MAGIC_EVIL_PATTERN"));
    }

    SECTION("the custom pattern is named in the detected patterns")
    {
        const auto patterns = SqlInjectionDetector::getDetectedPatterns("field = MAGIC_EVIL_PATTERN");
        REQUIRE_THAT(patterns, Catch::Matchers::Contains(std::string("Custom pattern matched: MAGIC_EVIL_PATTERN")));
    }

    SECTION("case insensitive")
    {
        REQUIRE(SqlInjectionDetector::isQuerySqlInjection("field = magic_evil_pattern"));
    }
}

TEST_CASE("SqlInjectionDetector keyword matching is case insensitive and word bounded", "[gatekeeper][sqli]")
{
    SqlInjectionDetector::initialize();

    REQUIRE(SqlInjectionDetector::isQuerySqlInjection("do a SHUTDOWN now"));
    REQUIRE(SqlInjectionDetector::isQuerySqlInjection("do a shutdown now"));
    SECTION("embedded in a word is not a keyword hit")
    {
        // 'shutdown' inside 'preshutdownx' is not a whole word
        REQUIRE_FALSE(SqlInjectionDetector::isQuerySqlInjection("preshutdownx"));
    }
}
