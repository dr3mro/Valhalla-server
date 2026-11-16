#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file cors_test.sh
#  * @brief Check that every CORS header appears exactly once on each probed API response.
#  * @details This script checks that CORS headers appear exactly once on API responses.
#  * It probes normal responses, preflights, and error responses such as 403, 429 and 503.
#  * Each case is recorded as a pass, a failure or a skip, and the script prints a total.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This guards against duplicate CORS headers, which browsers treat as an error.
#  * A back-end developer or CI runs it after changing server CORS or rate-limit settings.
#  * Without it a duplicate header could break the browser app while all other tests still pass.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - CORS explained: https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
#  * - The curl command: https://curl.se/docs/manpage.html
#  * - Bash shell functions: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Wire check for CORS headers: every CORS header must appear exactly once per response.
# Usage: ./cors_test.sh [base_url] [--strict]
#        STRICT=1 ./cors_test.sh [base_url]
# Env: COUNT=N requests for the 429 case (default 120), BAN_COUNT=N for the 503 case (default 0 = skip)
# No `-e`: each case records its own pass/fail/skip and the script reports an aggregate at the end.
set -uo pipefail  # Treat unset variables and failed pipeline steps as errors.

# Ambient defaults let CI select a case and turn skips into hard failures without changing the command line.
STRICT="${STRICT:-0}"  # When 1, a skipped case counts as a failure (set by CI).
CASE="${CASE:-all}"  # Which group of checks to run; defaults to all of them.
BASE="http://localhost:8080"  # Server address to test; a bare argument can replace it.
# Colour only when stdout is a terminal and NO_COLOR is unset, so redirected output stays clean.
if [[ -t 1 && -z ${NO_COLOR:-} ]]; then  # Use colours only for a terminal and only when NO_COLOR is not set.
    C_GREEN=$'\033[32m'
    C_RED=$'\033[31m'
    C_RESET=$'\033[0m'
else
    C_GREEN=""
    C_RED=""
    C_RESET=""
fi

# @brief Print a green "ok" line, stripping any existing "ok   " prefix.
# @param $1 Label to display.
ok_line() { printf '%sok%s   %s\n' "$C_GREEN" "$C_RESET" "${1#ok   }"; }
# @brief Print a red "FAIL" line, stripping any existing "FAIL " prefix.
# @param $1 Label to display.
fail_line() { printf '%sFAIL%s %s\n' "$C_RED" "$C_RESET" "${1#FAIL }"; }

# @brief Print the usage/help text stored in the script's own header comments.
usage() {
    # NOTE: --help prints a hard-coded slice of this file's own lines (lines 2 to 14).
    # Adding comment lines near the top shifts which lines that slice shows; the range is left unchanged on purpose.
    sed -n '2,14p' "$0"
}

# @brief Parse CLI flags into the global STRICT, CASE, BASE, SELF_TEST and PRINT_PARSED settings.
# @param $@... Command-line arguments; a bare positional value is treated as the base URL.
# @return 0 on success, 2 when --case is missing a value or has an invalid value.
# @note Called as `parse_args "$@" || exit 2`, so a bad invocation stops before any request is made.
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --strict) STRICT=1; shift ;;
            --case)
                if [[ $# -lt 2 ]]; then
                    printf 'error: --case needs a value (basic, 403, 429, 503, all)\n' >&2
                    return 2
                fi
                CASE="$2"
                shift 2
                ;;
            --case=*)
                CASE="${1#*=}"
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            --self-test)
                SELF_TEST=1
                shift
                ;;
            --print-parsed)
                PRINT_PARSED=1
                shift
                ;;
            *)
                BASE="$1"
                shift
                ;;
        esac
    done
    case "$CASE" in
        basic|403|429|503|all) ;;
        *)
            printf 'error: invalid case %q (valid: basic, 403, 429, 503, all)\n' "$CASE" >&2
            return 2
            ;;
    esac
    return 0
}

# Both flags are defined before parse_args because the --self-test/--print-parsed flags may set them.
SELF_TEST=0  # Flag for the built-in argument-parsing self test.
PRINT_PARSED=0  # Flag that prints the parsed options and quits without any request.
parse_args "$@" || exit 2  # Read the arguments first and stop before any request if they are invalid.

# Test-only mode used by --self-test: echo the resolved arguments and exit without contacting the server.
if [[ $PRINT_PARSED -eq 1 ]]; then  # Test mode: show what the parser understood, then exit.
    printf 'CASE=%s STRICT=%s BASE=%s\n' "$CASE" "$STRICT" "$BASE"
    exit 0
fi

if [[ $SELF_TEST -eq 1 ]]; then  # Test mode: check the argument parser against known inputs.
    fail=0
    # @brief Assert that --print-parsed resolves one argument set to the expected values.
    # @param $1..$3 Expected CASE, STRICT and BASE values.
    # @param $4... Arguments fed back into this script; the printed result is compared to the expectations.
    run_case() {
        local ec="$1" es="$2" eb="$3"
        shift 3
        local actual
        actual="$("$0" --print-parsed "$@" 2>/dev/null)" || { fail_line "FAIL $* -> command error"; fail=1; return; }
        if [[ $actual == "CASE=$ec STRICT=$es BASE=$eb" ]]; then
            ok_line "ok   $* -> $actual"
        else
            fail_line "FAIL $* -> $actual (expected CASE=$ec STRICT=$es BASE=$eb)"
            fail=1
        fi
    }
    run_case all 0 http://localhost:8080
    run_case 403 1 http://localhost:8080 --case 403 --strict
    run_case 403 0 http://x --case 403 http://x
    run_case 429 0 http://x --case=429 http://x
    run_case all 1 http://x --strict http://x
    run_case all 0 http://localhost:8080
    # error cases
    if "$0" --print-parsed --case >/dev/null 2>&1; then fail_line "FAIL --case alone should fail"; fail=1; else ok_line "ok   --case alone -> error"; fi
    if "$0" --print-parsed --case bogus >/dev/null 2>&1; then fail_line "FAIL --case bogus should fail"; fail=1; else ok_line "ok   --case bogus -> error"; fi
    printf '\nself-test: %d failure(s)\n' "$fail"
    exit "$fail"
fi

fail=0
passed=0
skipped=0  # Count of cases that could not be provoked on this server.
total=0  # Total number of checks attempted.

# @brief Count header occurrences case-insensitively; `|| true` absorbs grep's no-match status.
# @param $1 Raw header text; $2 header name without the colon.
count() { printf '%s\n' "$1" | grep -ic "^$2:" || true; }

# @brief Record a successful check and bump the pass/total counters.
# @param $1 Check label.
record_pass() { total=$((total + 1)); passed=$((passed + 1)); ok_line "ok   $1"; }
# @brief Record a failed check and bump the fail/total counters.
# @param $1 Check label.
record_fail() { total=$((total + 1)); fail=$((fail + 1)); fail_line "FAIL $1"; }
# @brief Record a skipped check; in STRICT mode a skip counts as a failure.
# @param $1 Check label plus the reason it was skipped.
record_skip() {
    total=$((total + 1))
    skipped=$((skipped + 1))
    printf 'SKIP %s\n' "$1"
    if [[ $STRICT == 1 ]]; then fail=$((fail + 1)); fi
}

# Fetch headers and the HTTP status code in one request. The status code comes
# from curl's -w, not from parsing the status line.
# @brief Run one curl request and return its headers and status through printf -v.
# @param $1 Variable name to receive headers (empty on a curl error).
# @param $2 Variable name to receive the status ("000" on a curl error).
# @param $3... Arguments forwarded to curl.
# @return 0 when curl completed the exchange, 1 on a transport failure; both variables are set either way.
fetch() { # fetch <variable name for headers> <variable name for status> <curl args...>
    local hvar="$1" svar="$2"
    shift 2
    local out out_status out_headers
    out="$(curl -sS -D - -o /dev/null -w $'\nHTTP_STATUS:%{http_code}' --max-time 10 "$@")" || { printf -v "$hvar" '%s' ""; printf -v "$svar" '%s' "000"; return 1; }
    out_status="${out##*HTTP_STATUS:}"
    out_headers="${out%HTTP_STATUS:*}"
    printf -v "$hvar" '%s' "$out_headers"
    printf -v "$svar" '%s' "$out_status"
}

# @brief Verify both the expected HTTP status and that each CORS header appears exactly once.
# @param $1 Human-readable check label; $2 expected HTTP status.
# @param $3... curl arguments (method, headers, URL, ...).
# @note Failures are recorded but never thrown, so the rest of the suite still runs.
check() { # check <label> <expected_status> <curl args...>
    local label="$1"
    local expected="$2"
    shift 2
    local headers status
    fetch headers status "$@" || { record_fail "$label (curl error)"; return; }
    if [[ $status != "$expected" ]]; then
        record_fail "$label (expected HTTP $expected, got ${status:-unknown})"
        return
    fi
    for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
        local n
        n="$(count "$headers" "$h")"
        if [[ $n -ne 1 ]]; then
            record_fail "$label ($h appears $n times)"
            return
        fi
    done
    record_pass "$label"
}

# The basic case covers ordinary responses and every preflight method; no auth token is needed here.
if [[ $CASE == "basic" || $CASE == "all" ]]; then  # Run the ordinary responses and preflight checks.
    check "GET /"               404 "$BASE/"
    check "GET unknown (404)"   404 "$BASE/api/v2/does-not-exist"
    check "POST no auth (401)"  401 -X POST -H 'Content-Type: application/json' -d '{}' "$BASE/api/v2/clients/users/read"
    check "preflight PUT"       204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: PUT' -H 'Access-Control-Request-Headers: content-type,authorization' "$BASE/api/v2/clients/users/update"
    check "preflight PATCH"     204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: PATCH' -H 'Access-Control-Request-Headers: content-type,authorization' "$BASE/api/v2/clients/users/update"
    check "preflight DELETE"    204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: DELETE' "$BASE/api/v2/clients/users/delete"
    check "unsupported method"  405 -X PATCH "$BASE/"
    check "HEAD /"              200 -I "$BASE/api/v2/hello"
    check "GET with Origin"     200 -H 'Origin: http://localhost:3000' "$BASE/api/v2/hello"

    # Allow-Methods check
    headers=""
    status=""
    fetch headers status -X OPTIONS -H 'Access-Control-Request-Method: PUT' "$BASE/api/v2/clients/users/update" || true
    methods="$(printf '%s\n' "$headers" | grep -i '^access-control-allow-methods:' || true)"
    if [[ $methods == *PUT* && $methods == *PATCH* && $methods == *DELETE* ]]; then
        record_pass "preflight Allow-Methods contains PUT, PATCH and DELETE"
    else
        record_fail "preflight Allow-Methods missing PUT/PATCH/DELETE"
    fi
fi

# 403/503/429 depend on server-side throttling state, so those cases may be skipped instead of failed.
if [[ $CASE == "403" || $CASE == "all" ]]; then  # Run the blacklisted-client case when selected.
    # Blacklisted (403): only meaningful if the server blacklists this client IP.
    headers=""
    status=""
    fetch headers status "$BASE/api/v2/hello" || true
    if [[ $status == "403" ]]; then
        bad=0
        for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
            n="$(count "$headers" "$h")"
            if [[ $n -ne 1 ]]; then record_fail "blacklisted (403) ($h appears $n times)"; bad=1; break; fi
        done
        [[ $bad -eq 0 ]] && record_pass "blacklisted (403)"
    else
        record_skip "blacklisted (403): server not blacklisting this client (last status ${status:-unknown}); set BLACKLIST to include the client IP to test"
    fi
fi

if [[ $CASE == "503" || $CASE == "all" ]]; then  # Run the banned-client case when selected.
    # Banned (503): many identical requests from one IP.
    # Default 0 skips this case because provoking a ban can block this client for the configured duration.
    BAN_COUNT="${BAN_COUNT:-0}"  # How many requests to send to provoke a ban; 0 means skip this case.
    if [[ $BAN_COUNT -le 0 ]]; then  # No ban requested, so record a skip.
        record_skip "banned (503): set BAN_COUNT=10001 (or higher) to provoke"
    else
        code=""
        last_headers=""
        for i in $(seq 1 "$BAN_COUNT"); do
            fetch last_headers code "$BASE/api/v2/hello" || true
            if [[ $code == "503" ]]; then break; fi
        done
        if [[ $code != "503" ]]; then
            record_skip "banned (503): not provoked after $BAN_COUNT requests (last status ${code:-unknown})"
        else
            bad=0
            for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
                n="$(count "$last_headers" "$h")"
                if [[ $n -ne 1 ]]; then record_fail "banned (503) ($h appears $n times)"; bad=1; break; fi
            done
            [[ $bad -eq 0 ]] && record_pass "banned (503)"
        fi
    fi
fi

if [[ $CASE == "429" || $CASE == "all" ]]; then  # Run the rate-limited case when selected.
    # Rate limited (429): many distinct requests from one IP.
    # X-Test-Id makes each request distinct so the request counter, not the duplicate filter, triggers the 429.
    COUNT="${COUNT:-120}"  # How many requests to send when trying to trigger a 429.
    echo "note: 429 case assumes the default MAX_FPS=100 (defaults.hpp); COUNT=$COUNT; .env may override these thresholds"
    code=""
    last_headers=""
    for i in $(seq 1 "$COUNT"); do  # Send requests one at a time until a 429 appears.
        fetch last_headers code -H "X-Test-Id: $i" "$BASE/api/v2/hello" || true
        if [[ $code == "429" ]]; then break; fi
    done
    if [[ $code != "429" ]]; then
        record_skip "rate limited (429): not provoked after $COUNT requests (last status ${code:-unknown})"
    else
        bad=0
        for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
            n="$(count "$last_headers" "$h")"
            if [[ $n -ne 1 ]]; then record_fail "rate limited (429) ($h appears $n times)"; bad=1; break; fi
        done
        [[ $bad -eq 0 ]] && record_pass "rate limited (429)"
    fi
fi

printf '\n%d passed, %d failed, %d skipped\n' "$passed" "$fail" "$skipped"  # Print the final totals.
# The burst loops above leave this client throttled/banned for a while, so warn before exiting.
echo "warning: this client IP may now be rate-limited or banned for the configured duration"  # Warn that the burst loops may have throttled this machine.

# Any failure (or a STRICT skip) yields exit 1; a clean run exits 0.
if [[ $fail -gt 0 ]]; then  # Any failure (or a strict skip) makes the whole run fail.
    exit 1
fi
exit 0
