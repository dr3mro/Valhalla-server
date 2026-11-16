#!/usr/bin/env bash
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
set -uo pipefail

BASE="http://localhost:8080"
CREDENTIALS=0
ORIGIN_LIST=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --credentials) CREDENTIALS=1 ;;
        --origin) shift; ORIGIN_LIST+=("${1:?--origin needs a value}") ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) BASE="$1" ;;
    esac
    shift
done
if [[ ${#ORIGIN_LIST[@]} -eq 0 ]]; then
    if [[ -n ${ORIGINS:-} ]]; then read -r -a ORIGIN_LIST <<<"$ORIGINS"
    else ORIGIN_LIST=("http://localhost:3000" "http://localhost:5173" "http://127.0.0.1:3000"); fi
fi
PREFLIGHT_BURST="${PREFLIGHT_BURST:-60}"
API="$BASE/api/v2"

passed=0; failed=0
# Set indirectly by fetch() through printf -v; initialised here so the assignments are visible to shellcheck.
ph=""; ps=""; rh=""; rs=""
pass() { passed=$((passed + 1)); printf 'ok   %s\n' "$1"; }
fail() { failed=$((failed + 1)); printf 'FAIL %s\n' "$1"; [[ -n ${2:-} ]] && printf '       %s\n' "$2"; }

# fetch <headers var> <status var> <curl args...>; status comes from curl, headers from the dump.
fetch() {
    local hvar="$1" svar="$2"; shift 2
    local out
    out="$(curl -sS -D - -o /dev/null -w $'\nHTTP_STATUS:%{http_code}' --max-time 10 "$@" 2>/dev/null)" \
        || { printf -v "$hvar" '%s' ""; printf -v "$svar" '%s' "000"; return 1; }
    printf -v "$svar" '%s' "${out##*HTTP_STATUS:}"
    printf -v "$hvar" '%s' "${out%HTTP_STATUS:*}"
}

# header_values <headers> <name>: one line per occurrence, value only (CR removed, name case-insensitive).
header_values() {
    printf '%s\n' "$1" | tr -d '\r' | awk -v n="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')" '
        { i = index($0, ":"); if (i > 0 && tolower(substr($0, 1, i - 1)) == n) { v = substr($0, i + 1); sub(/^[ \t]+/, "", v); sub(/[ \t]+$/, "", v); print v } }'
}
header_count() { header_values "$1" "$2" | grep -c . || true; }

# list_has <comma list> <item>: case-insensitive membership test for a comma separated header value.
list_has() {
    local item
    item="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')"
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr ',' '\n' | sed 's/^[ \t]*//; s/[ \t]*$//' | grep -Fxq -- "$item"
}

# Apply the browser's preflight rules. Echoes nothing; returns 0 and sets REASON on failure.
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

# Scenarios a React app produces. Each triggers a preflight because of JSON content-type and/or Authorization.
# label|METHOD|path|preflight Access-Control-Request-Headers (as browsers send: lowercase, sorted, comma separated)
SCENARIOS=(
    "login (POST, JSON)|POST|/clients/users/login|content-type"
    "read (POST, JSON + bearer)|POST|/clients/users/read|authorization,content-type"
    "update (PUT, JSON + bearer)|PUT|/clients/users/update|authorization,content-type"
    "delete (DELETE, JSON + bearer)|DELETE|/clients/users/delete|authorization,content-type"
    "services (GET + bearer)|GET|/clients/users/services|authorization"
)

if ! fetch _probe_headers _probe_status "$BASE/api/v2/hello"; then
    printf 'FAIL cannot reach %s (is the container running and mapped with -p 8080:8080? try: docker ps)\n' "$BASE"
    exit 1
fi

for origin in "${ORIGIN_LIST[@]}"; do
    printf '\n== origin %s ==\n' "$origin"
    for sc in "${SCENARIOS[@]}"; do
        IFS='|' read -r label method path reqh <<<"$sc"
        # 1) the preflight the browser sends first: no credentials, no auth header, the real method in Access-Control-Request-Method
        fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: $method" \
            -H "Access-Control-Request-Headers: $reqh" "$API$path" || { fail "$label: preflight curl error"; continue; }
        if ! preflight_ok "$ps" "$ph" "$origin" "$method" "$reqh"; then fail "$label: preflight rejected by browser rules" "$REASON"; continue; fi
        pass "$label: preflight accepted ($ps)"
        # 2) the real request after the preflight. A fake token is used so nothing is changed; 4xx is expected and fine,
        #    what matters is that the response is readable by the page (Allow-Origin present exactly once).
        args=(-X "$method" -H "Origin: $origin")
        [[ $reqh == *content-type* ]] && args+=(-H "Content-Type: application/json" -d '{}')
        [[ $reqh == *authorization* ]] && args+=(-H "Authorization: Bearer cors.check.invalid")
        fetch rh rs "${args[@]}" "$API$path" || { fail "$label: real request curl error"; continue; }
        n="$(header_count "$rh" access-control-allow-origin)"
        ao="$(header_values "$rh" access-control-allow-origin)"
        if [[ $n -ne 1 ]]; then fail "$label: real response ($rs) has Access-Control-Allow-Origin $n times, the page could not read it"; continue; fi
        if [[ $CREDENTIALS -eq 1 && ( $ao != "$origin" || "$(header_values "$rh" access-control-allow-credentials)" != "true" ) ]]; then
            fail "$label: real response ($rs) not usable with credentials" "Allow-Origin='$ao'"; continue
        fi
        if [[ $CREDENTIALS -eq 0 && $ao != "*" && $ao != "$origin" ]]; then fail "$label: real response ($rs) Allow-Origin='$ao'"; continue; fi
        pass "$label: real response readable by the page (HTTP $rs)"
    done
done

printf '\n== other things React triggers ==\n'
origin="${ORIGIN_LIST[0]}"
# Preflight for a path that does not exist: the browser preflights before the route is known.
fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: PUT" -H "Access-Control-Request-Headers: content-type" "$API/clients/users/does-not-exist" || true
if preflight_ok "$ps" "$ph" "$origin" PUT content-type; then pass "preflight to an unknown path accepted"; else fail "preflight to an unknown path" "$REASON"; fi
# A custom request header (for example an axios interceptor adding X-Request-Id).
fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: POST" -H "Access-Control-Request-Headers: content-type,x-request-id" "$API/clients/users/login" || true
if preflight_ok "$ps" "$ph" "$origin" POST content-type,x-request-id; then pass "custom request header x-request-id allowed"; else fail "custom request header x-request-id" "$REASON"; fi
# Preflight must not need authentication and must not be rate limited (React StrictMode and polling repeat calls).
bad=0
for i in $(seq 1 "$PREFLIGHT_BURST"); do
    fetch ph ps -X OPTIONS -H "Origin: $origin" -H "Access-Control-Request-Method: POST" -H "Access-Control-Request-Headers: authorization,content-type" "$API/clients/users/read" || true
    if ! preflight_ok "$ps" "$ph" "$origin" POST authorization,content-type; then bad=1; fail "preflight burst: request $i of $PREFLIGHT_BURST" "$REASON"; break; fi
done
[[ $bad -eq 0 ]] && pass "$PREFLIGHT_BURST repeated preflights all accepted (not rate limited)"
# Headers the page needs to read.
fetch rh rs -H "Origin: $origin" "$API/clients/users/services" || true
if list_has "$(header_values "$rh" access-control-expose-headers)" "retry-after"; then pass "Retry-After is exposed to JavaScript"; else fail "Retry-After not in Access-Control-Expose-Headers"; fi
# Parse-level errors cannot carry CORS headers (known Drogon limitation, README): informational only.
printf 'note  malformed requests (HTTP 400 from the parser) carry no CORS headers; browsers show those as CORS errors (known limitation)\n'
if [[ $CREDENTIALS -eq 0 ]]; then
    printf 'note  Allow-Origin is "*": it works unless the front end uses cookies or withCredentials; re-run with --credentials to test that mode\n'
fi

printf '\n%d passed, %d failed\n' "$passed" "$failed"
[[ $failed -eq 0 ]]
