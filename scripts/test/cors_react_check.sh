#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file cors_react_check.sh
#  * @brief Verify CORS preflight and real responses against browser Fetch rules for React origins.
#  * @details This script checks CORS behaviour the way a web browser would.
#  * It sends the preflight and the real request that a React front end makes from another origin.
#  * It applies the Fetch standard rules and exits successfully only if every check passes.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This protects the browser-facing side of the API used by the React app.
#  * A front-end or back-end developer runs it after changing CORS settings, or in CI.
#  * Without it a wrong CORS header could silently block the web app in real browsers.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - CORS explained: https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
#  * - The Fetch standard (preflight rules): https://fetch.spec.whatwg.org/
#  * - The curl command: https://curl.se/docs/manpage.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Simulates what a browser does before and after a cross-origin call from a React dev server
# (http://localhost:3000, :5173, ...) to this API (default http://localhost:8080, e.g. Docker -p 8080:8080),
# and applies the same acceptance rules a browser applies to the CORS preflight (Fetch standard).
#
# Usage: ./cors_react_check.sh [base_url] [--credentials] [--origin URL]...
#   --credentials   the front end sends cookies/withCredentials (axios withCredentials: true, fetch credentials: "include").
#                   A wildcard Access-Control-Allow-Origin is then rejected by browsers, so this mode requires the exact
#                   origin to be echoed plus Access-Control-Allow-Credentials: true.
#   --origin URL    add an origin to test (repeatable). Default: http://localhost:3000, http://localhost:5173, http://127.0.0.1:3000
# Env: ORIGINS="http://a http://b" (space separated, same as --origin), PREFLIGHT_BURST=N (default 60).
# Exit code is 0 only if every check passes.
# No `-e`: failed probes are recorded and the remaining checks still run, so one bad response does not
# hide the rest of the report.
set -uo pipefail  # Treat unset variables and failed pipeline steps as errors.

BASE="http://localhost:8080"  # Server address to test; a bare argument can replace it.
CREDENTIALS=0  # Start in the mode that does not send cookies (0 = off).
ORIGIN_LIST=()  # Empty list that will hold the origins to test.
# Only emit ANSI colours when stdout is a terminal and NO_COLOR is unset; CI/logs stay plain text.
if [[ -t 1 && -z ${NO_COLOR:-} ]]; then  # Use colours only when printing to a terminal and NO_COLOR is not set.
    C_GREEN=$'\033[32m'
    C_RED=$'\033[31m'
    C_RESET=$'\033[0m'
else
    C_GREEN=""
    C_RED=""
    C_RESET=""
fi
while [[ $# -gt 0 ]]; do  # Look at each command-line argument in turn.
    case "$1" in  # Work out what the current argument means.
        --credentials) CREDENTIALS=1 ;;  # Switch on the cookie/withCredentials mode.
        --origin) shift; ORIGIN_LIST+=("${1:?--origin needs a value}") ;;  # Add a custom origin to the list and fail if no value follows the flag.
        # NOTE: --help prints a hard-coded slice of this file's own lines (lines 2 to 14).
        # Adding comment lines near the top shifts which lines that slice shows; the range is left unchanged on purpose.
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) BASE="$1" ;;  # Treat any other bare value as the server address.
    esac
    shift  # Move on to the next argument.
done
# Fall back to the ORIGINS environment variable, then to the common React dev-server origins.
if [[ ${#ORIGIN_LIST[@]} -eq 0 ]]; then  # If the user supplied no origins, choose defaults below.
    if [[ -n ${ORIGINS:-} ]]; then read -r -a ORIGIN_LIST <<<"$ORIGINS"  # The ORIGINS environment variable can provide a space-separated list.
    else ORIGIN_LIST=("http://localhost:3000" "http://localhost:5173" "http://127.0.0.1:3000"); fi  # Otherwise use the usual React development addresses.
fi
# Burst length for the "preflights are not rate limited" check (React StrictMode/polling reproduce this).
PREFLIGHT_BURST="${PREFLIGHT_BURST:-60}"  # How many repeated preflights to send in the 'not rate limited' check.
API="$BASE/api/v2"  # Full prefix added in front of every API path below.

passed=0; failed=0  # Counters for the final pass/fail report.
# @brief Print a green "ok" line, stripping any existing "ok   " prefix to avoid duplicates.
# @param $1 Label to display.
ok_line() { printf '%sok%s   %s\n' "$C_GREEN" "$C_RESET" "${1#ok   }"; }
# @brief Print a red "FAIL" line, stripping any existing "FAIL " prefix.
# @param $1 Label to display.
fail_line() { printf '%sFAIL%s %s\n' "$C_RED" "$C_RESET" "${1#FAIL }"; }
# Set indirectly by fetch() through printf -v; initialised here so the assignments are visible to shellcheck.
ph=""; ps=""; rh=""; rs=""  # Empty slots for preflight headers/status and real-response headers/status.
# @brief Record one passing check and print it.
# @param $1 Label describing the check.
pass() { passed=$((passed + 1)); ok_line "ok   $1"; }
# @brief Record one failed check, print it and optionally print an indented detail line.
# @param $1 Label describing the check.
# @param $2 Optional reason/detail shown on the next line.
fail() { failed=$((failed + 1)); fail_line "FAIL $1"; [[ -n ${2:-} ]] && printf '       %s\n' "$2"; }

# fetch <headers var> <status var> <curl args...>; status comes from curl, headers from the dump.
# @brief Issue one curl request, splitting its -D header dump from its -w status suffix.
# @param $1 Name of the variable to receive the response headers (via printf -v).
# @param $2 Name of the variable to receive the HTTP status; "000" on a transport failure.
# @param $3... Arguments passed straight to curl.
# @return 0 on a completed HTTP exchange, 1 if curl itself failed (both variables are still set).
# @note stderr is suppressed so transport noise does not pollute the report; callers treat failures as check failures.
fetch() {
    local hvar="$1" svar="$2"; shift 2
    local out
    out="$(curl -sS -D - -o /dev/null -w $'\nHTTP_STATUS:%{http_code}' --max-time 10 "$@" 2>/dev/null)" \
        || { printf -v "$hvar" '%s' ""; printf -v "$svar" '%s' "000"; return 1; }
    printf -v "$svar" '%s' "${out##*HTTP_STATUS:}"
    printf -v "$hvar" '%s' "${out%HTTP_STATUS:*}"
}

# header_values <headers> <name>: one line per occurrence, value only (CR removed, name case-insensitive).
# @brief Extract every value of a named header from a raw dump.
# @param $1 Raw header text as captured by fetch.
# @param $2 Header name, matched case-insensitively.
# @return One trimmed value per line on stdout; nothing when the header is absent.
header_values() {
    printf '%s\n' "$1" | tr -d '\r' | awk -v n="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')" '
        { i = index($0, ":"); if (i > 0 && tolower(substr($0, 1, i - 1)) == n) { v = substr($0, i + 1); sub(/^[ \t]+/, "", v); sub(/[ \t]+$/, "", v); print v } }'
}
# @brief Count how many times a header appears in a dump.
# @param $1 Raw header text.
# @param $2 Header name (case-insensitive).
# @note `|| true` absorbs grep's no-match status so the count is still 0.
header_count() { header_values "$1" "$2" | grep -c . || true; }

# list_has <comma list> <item>: case-insensitive membership test for a comma separated header value.
# @brief Test whether an item appears in a comma-separated header value (whitespace ignored, case-insensitive).
# @param $1 Comma-separated list, e.g. an Access-Control-Allow-* value.
# @param $2 Exact item to look for.
# @return 0 when present, 1 otherwise.
list_has() {
    local item
    item="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')"
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr ',' '\n' | sed 's/^[ \t]*//; s/[ \t]*$//' | grep -Fxq -- "$item"
}

# Apply the browser's preflight rules. Echoes nothing; returns 0 and sets REASON on failure.
# @brief Validate an OPTIONS response using the Fetch standard's CORS preflight acceptance rules.
# @param $1 Preflight HTTP status (must be 2xx; browsers do not follow redirects).
# @param $2 Raw response headers from fetch.
# @param $3 Request Origin.
# @param $4 Requested method (Access-Control-Request-Method).
# @param $5 Comma-separated Access-Control-Request-Headers.
# @return 0 if a browser would accept the preflight, 1 otherwise.
# @note On failure the global REASON holds a human-readable explanation; nothing is echoed.
preflight_ok() { # preflight_ok <status> <headers> <origin> <method> <request headers (comma)>
    local status="$1" headers="$2" origin="$3" method="$4" reqh="$5"
    REASON=""
    if [[ $status != 2* ]]; then REASON="preflight status is $status, browsers require 2xx (and do not follow redirects)"; return 1; fi
    local n; n="$(header_count "$headers" access-control-allow-origin)"
    if [[ $n -ne 1 ]]; then REASON="Access-Control-Allow-Origin appears $n times (must be exactly once)"; return 1; fi
    local ao; ao="$(header_values "$headers" access-control-allow-origin)"
    if [[ $CREDENTIALS -eq 1 ]]; then
        if [[ $ao != "$origin" ]]; then REASON="credentials mode needs Allow-Origin == $origin, got '$ao' (wildcard is rejected with credentials)"; return 1; fi
        if [[ "$(header_values "$headers" access-control-allow-credentials)" != "true" ]]; then REASON="credentials mode needs Access-Control-Allow-Credentials: true"; return 1; fi
    elif [[ $ao != "*" && $ao != "$origin" ]]; then
        REASON="Allow-Origin is '$ao', expected '*' or '$origin'"; return 1
    fi
    local am; am="$(header_values "$headers" access-control-allow-methods)"
    if [[ $(header_count "$headers" access-control-allow-methods) -ne 1 ]]; then REASON="Access-Control-Allow-Methods appears $(header_count "$headers" access-control-allow-methods) times (must be exactly once)"; return 1; fi
    if ! printf '%s' "$am" | tr ',' '\n' | sed 's/^[ \t]*//; s/[ \t]*$//' | grep -Fxq -- "$method"; then
        REASON="Access-Control-Allow-Methods '$am' does not include $method"; return 1
    fi
    if [[ -n $reqh ]]; then
        if [[ $(header_count "$headers" access-control-allow-headers) -ne 1 ]]; then REASON="Access-Control-Allow-Headers appears $(header_count "$headers" access-control-allow-headers) times (must be exactly once)"; return 1; fi
        local ah h; ah="$(header_values "$headers" access-control-allow-headers)"
        local IFS=','
        for h in $reqh; do
            # The '*' wildcard never covers Authorization, and is not honoured with credentials.
            if ! list_has "$ah" "$h"; then
                if [[ $ah == "*" && $h != authorization && $CREDENTIALS -eq 0 ]]; then continue; fi
                REASON="Access-Control-Allow-Headers '$ah' does not allow request header '$h'"; return 1
            fi
        done
    fi
    return 0
}

# Scenarios a React app produces. Each triggers a preflight because of JSON content-type,
# Authorization, or a non-safelisted client header.
# label|METHOD|path|preflight Access-Control-Request-Headers (as browsers send: lowercase, sorted, comma separated)
SCENARIOS=(
    "login (POST, JSON)|POST|/clients/users/login|content-type"
    "read (POST, JSON + bearer)|POST|/clients/users/read|authorization,content-type"
    "update (PUT, JSON + bearer)|PUT|/clients/users/update|authorization,content-type"
    "partial update (PATCH, JSON + bearer)|PATCH|/clients/users/update|authorization,content-type"
    "delete (DELETE, JSON + bearer)|DELETE|/clients/users/delete|authorization,content-type"
    "services (GET + bearer)|GET|/clients/users/services|authorization"
    "services (GET + bearer + Accept)|GET|/clients/users/services|authorization"
    "login (POST, JSON + axios header)|POST|/clients/users/login|content-type,x-requested-with"
    "read (POST, JSON + request id)|POST|/clients/users/read|authorization,content-type,x-request-id"
)

# Fail early with a useful hint when the API is unreachable; later checks would all be meaningless.
if ! fetch _probe_headers _probe_status "$BASE/api/v2/hello"; then  # Check the server is reachable first; later checks would be meaningless otherwise.
    fail_line "FAIL cannot reach $BASE (is the container running and mapped with -p 8080:8080? try: docker ps)"
    exit 1
fi

for origin in "${ORIGIN_LIST[@]}"; do  # Repeat the whole suite for each origin.
    printf '\n== origin %s ==\n' "$origin"
    for sc in "${SCENARIOS[@]}"; do  # Repeat the checks for each request scenario.
        IFS='|' read -r label method path reqh <<<"$sc"  # Split the scenario line on the pipe character into four fields.
        # 1) the preflight the browser sends first: no credentials, no auth header, the real method in Access-Control-Request-Method
        fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: $method" \
            -H "Access-Control-Request-Headers: $reqh" "$API$path" || { fail "$label: preflight curl error"; continue; }
        if ! preflight_ok "$ps" "$ph" "$origin" "$method" "$reqh"; then fail "$label: preflight rejected by browser rules" "$REASON"; continue; fi
        pass "$label: preflight accepted ($ps)"
        # 2) the real request after the preflight. A fake token is used so nothing is changed; 4xx is expected and fine,
        #    what matters is that the response is readable by the page (Allow-Origin present exactly once).
        # Build the real request from the scenario; the bearer value is a deliberately invalid fixture,
        # since only CORS readability is under test, not authentication.
        args=(-X "$method" -H "Origin: $origin")  # Start building the real request's arguments.
        [[ $reqh == *content-type* ]] && args+=(-H "Content-Type: application/json" -d '{}')  # Add a JSON body when the scenario calls for a content-type.
        [[ $reqh == *authorization* ]] && args+=(-H "Authorization: Bearer cors.check.invalid")  # Add a deliberately fake token fixture; only CORS readability is under test.
        [[ $label == *Accept* ]] && args+=(-H "Accept: application/json")  # Add the Accept header when the scenario names it.
        fetch rh rs "${args[@]}" "$API$path" || { fail "$label: real request curl error"; continue; }
        n="$(header_count "$rh" access-control-allow-origin)"  # Count how many times the readable-by-JavaScript header appears.
        ao="$(header_values "$rh" access-control-allow-origin)"  # Read the value the page would actually see.
        if [[ $n -ne 1 ]]; then fail "$label: real response ($rs) has Access-Control-Allow-Origin $n times, the page could not read it"; continue; fi
        if [[ $CREDENTIALS -eq 1 && ( $ao != "$origin" || "$(header_values "$rh" access-control-allow-credentials)" != "true" ) ]]; then
            fail "$label: real response ($rs) not usable with credentials" "Allow-Origin='$ao'"; continue
        fi
        if [[ $CREDENTIALS -eq 0 && $ao != "*" && $ao != "$origin" ]]; then fail "$label: real response ($rs) Allow-Origin='$ao'"; continue; fi
        pass "$label: real response readable by the page (HTTP $rs)"
    done
done

printf '\n== other things React triggers ==\n'
origin="${ORIGIN_LIST[0]}"  # Use the first origin for the extra one-off checks.
# Preflight for a path that does not exist: the browser preflights before the route is known.
fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: PUT" -H "Access-Control-Request-Headers: content-type" "$API/clients/users/does-not-exist" || true
if preflight_ok "$ps" "$ph" "$origin" PUT content-type; then pass "preflight to an unknown path accepted"; else fail "preflight to an unknown path" "$REASON"; fi
# Custom request headers commonly added by axios interceptors.
fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: POST" -H "Access-Control-Request-Headers: content-type,x-request-id" "$API/clients/users/login" || true
if preflight_ok "$ps" "$ph" "$origin" POST content-type,x-request-id; then pass "custom request header x-request-id allowed"; else fail "custom request header x-request-id" "$REASON"; fi
# The browser caches successful preflights using this response header.
max_age="$(header_values "$ph" access-control-max-age)"  # Read how long a browser may cache a successful preflight.
if [[ $max_age == 7200 ]]; then
    pass "preflight max-age is 7200"
else
    fail "preflight max-age" "expected 7200, got '$max_age'"
fi
# Preflight must not need authentication and must not be rate limited (React StrictMode and polling repeat calls).
bad=0  # Flag that stays 0 until a preflight in the burst fails.
for i in $(seq 1 "$PREFLIGHT_BURST"); do  # Send the burst one request at a time.
    fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: POST" -H "Access-Control-Request-Headers: authorization,content-type" "$API/clients/users/read" || true
    if ! preflight_ok "$ps" "$ph" "$origin" POST authorization,content-type; then bad=1; fail "preflight burst: request $i of $PREFLIGHT_BURST" "$REASON"; break; fi
done
[[ $bad -eq 0 ]] && pass "$PREFLIGHT_BURST repeated preflights all accepted (not rate limited)"
# Headers the page needs to read.
fetch rh rs -H "Origin: $origin" "$API/clients/users/services" || true
if list_has "$(header_values "$rh" access-control-expose-headers)" "retry-after"; then pass "Retry-After is exposed to JavaScript"; else fail "Retry-After not in Access-Control-Expose-Headers"; fi
if list_has "$(header_values "$rh" access-control-expose-headers)" "content-length"; then pass "Content-Length is exposed to JavaScript"; else fail "Content-Length not in Access-Control-Expose-Headers"; fi
# Parse-level errors cannot carry CORS headers (known Drogon limitation, README): informational only.
printf 'note  malformed requests (HTTP 400 from the parser) carry no CORS headers; browsers show those as CORS errors (known limitation)\n'
if [[ $CREDENTIALS -eq 0 ]]; then
    printf 'note  Allow-Origin is "*": it works unless the front end uses cookies or withCredentials; re-run with --credentials to test that mode\n'
fi

printf '\n%d passed, %d failed\n' "$passed" "$failed"  # Print the final tally of passing and failing checks.
# With no `-e`, the final `[[ ... ]]` is also the script's exit status: success only when nothing failed.
[[ $failed -eq 0 ]]  # The last command is also the exit status: success only with no failures.
