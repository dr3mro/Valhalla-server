#!/usr/bin/env bash
# Wire check for CORS headers: every CORS header must appear exactly once per response.
# Usage: ./cors_test.sh [base_url] [--strict]
#        STRICT=1 ./cors_test.sh [base_url]
# Env: COUNT=N requests for the 429 case (default 120), BAN_COUNT=N for the 503 case (default 0 = skip)
set -uo pipefail

STRICT="${STRICT:-0}"
BASE="http://localhost:8080"
for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        -h|--help)
            sed -n '2,8p' "$0"
            exit 0
            ;;
        *) BASE="$arg" ;;
    esac
done

fail=0
passed=0
skipped=0
total=0

count() { printf '%s\n' "$1" | grep -ic "^$2:" || true; }

status_of() { printf '%s\n' "$1" | sed -n '1s/^HTTP\/[^ ]* \([0-9][0-9][0-9]\).*/\1/p' | head -1; }

record_pass() { total=$((total + 1)); passed=$((passed + 1)); printf 'ok   %s\n' "$1"; }
record_fail() { total=$((total + 1)); fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }
record_skip() {
    total=$((total + 1))
    skipped=$((skipped + 1))
    printf 'SKIP %s\n' "$1"
    if [[ $STRICT == 1 ]]; then fail=$((fail + 1)); fi
}

check() { # check <label> <expected_status> <curl args...>
    local label="$1"
    local expected="$2"
    shift 2
    local out status
    out="$(curl -si --max-time 10 "$@")" || { record_fail "$label (curl error)"; return; }
    status="$(status_of "$out")"
    if [[ $status != "$expected" ]]; then
        record_fail "$label (expected HTTP $expected, got ${status:-unknown})"
        return
    fi
    for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
        local n
        n="$(count "$out" "$h")"
        if [[ $n -ne 1 ]]; then
            record_fail "$label ($h appears $n times)"
            return
        fi
    done
    record_pass "$label"
}

check "GET /"               404 "$BASE/"
check "GET unknown (404)"   404 "$BASE/api/v2/does-not-exist"
check "POST no auth (401)"  401 -X POST -H 'Content-Type: application/json' -d '{}' "$BASE/api/v2/clients/users/read"
check "preflight PUT"       204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: PUT' -H 'Access-Control-Request-Headers: content-type,authorization' "$BASE/api/v2/clients/users/update"
check "preflight DELETE"    204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: DELETE' "$BASE/api/v2/clients/users/delete"
check "unsupported method"  405 -X PATCH "$BASE/"
check "HEAD /"              200 -I "$BASE/api/v2/hello"
check "GET with Origin"     200 -H 'Origin: http://localhost:3000' "$BASE/api/v2/hello"

# Allow-Methods check
methods="$(curl -si -X OPTIONS -H 'Access-Control-Request-Method: PUT' "$BASE/api/v2/clients/users/update" | grep -i '^access-control-allow-methods:' || true)"
if [[ $methods == *PUT* && $methods == *DELETE* ]]; then
    record_pass "preflight Allow-Methods contains PUT and DELETE"
else
    record_fail "preflight Allow-Methods missing PUT/DELETE"
fi

# Blacklisted (403): only meaningful if the server blacklists this client IP.
code=""
last="$(curl -si --max-time 10 "$BASE/api/v2/hello")" || true
code="$(status_of "$last")"
if [[ $code == "403" ]]; then
    for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
        n="$(count "$last" "$h")"
        if [[ $n -ne 1 ]]; then record_fail "blacklisted (403) ($h appears $n times)"; break; fi
    done
    [[ $n -eq 1 ]] && record_pass "blacklisted (403)"
else
    record_skip "blacklisted (403): server not blacklisting this client (last status ${code:-unknown}); set BLACKLIST to include the client IP to test"
fi

# Banned (503): many identical requests from one IP.
BAN_COUNT="${BAN_COUNT:-0}"
if [[ $BAN_COUNT -le 0 ]]; then
    record_skip "banned (503): set BAN_COUNT=10001 (or higher) to provoke"
else
    code=""
    last=""
    for i in $(seq 1 "$BAN_COUNT"); do
        last="$(curl -si --max-time 10 "$BASE/api/v2/hello")" || true
        code="$(status_of "$last")"
        if [[ $code == "503" ]]; then break; fi
    done
    if [[ $code != "503" ]]; then
        record_skip "banned (503): not provoked after $BAN_COUNT requests (last status ${code:-unknown})"
    else
        bad=0
        for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
            n="$(count "$last" "$h")"
            if [[ $n -ne 1 ]]; then record_fail "banned (503) ($h appears $n times)"; bad=1; break; fi
        done
        [[ $bad -eq 0 ]] && record_pass "banned (503)"
    fi
fi

# Rate limited (429): many distinct requests from one IP.
# Default thresholds from src/configurator/defaults.hpp: MAX_FPS=100, MAX_REQUESTS=10000.
COUNT="${COUNT:-120}"
echo "note: 429 case assumes the default MAX_FPS=100 (defaults.hpp); COUNT=$COUNT; .env may override these thresholds"
code=""
last=""
for i in $(seq 1 "$COUNT"); do
    last="$(curl -si --max-time 10 -H "X-Test-Id: $i" "$BASE/api/v2/hello")" || true
    code="$(status_of "$last")"
    if [[ $code == "429" ]]; then break; fi
done
if [[ $code != "429" ]]; then
    record_skip "rate limited (429): not provoked after $COUNT requests (last status ${code:-unknown})"
else
    bad=0
    for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
        n="$(count "$last" "$h")"
        if [[ $n -ne 1 ]]; then record_fail "rate limited (429) ($h appears $n times)"; bad=1; break; fi
    done
    [[ $bad -eq 0 ]] && record_pass "rate limited (429)"
fi

printf '\n%d passed, %d failed, %d skipped\n' "$passed" "$fail" "$skipped"
echo "warning: this client IP may now be rate-limited or banned for the configured duration"

if [[ $fail -gt 0 ]]; then
    exit 1
fi
exit 0
