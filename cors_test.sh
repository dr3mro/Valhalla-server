#!/usr/bin/env bash
# Wire check for CORS headers: every CORS header must appear exactly once per response.
# Usage: ./cors_test.sh [base_url] [--strict]
#        STRICT=1 ./cors_test.sh [base_url]
# Env: COUNT=N requests for the 429 case (default 120), BAN_COUNT=N for the 503 case (default 0 = skip)
set -uo pipefail

STRICT="${STRICT:-0}"
CASE="${CASE:-all}"
BASE="http://localhost:8080"
for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        --case) shift; CASE="${1:-all}" ;;
        --case=*) CASE="${arg#*=}" ;;
        -h|--help)
            sed -n '2,10p' "$0"
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

record_pass() { total=$((total + 1)); passed=$((passed + 1)); printf 'ok   %s\n' "$1"; }
record_fail() { total=$((total + 1)); fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }
record_skip() {
    total=$((total + 1))
    skipped=$((skipped + 1))
    printf 'SKIP %s\n' "$1"
    if [[ $STRICT == 1 ]]; then fail=$((fail + 1)); fi
}

# Fetch headers and the HTTP status code in one request. The status code comes
# from curl's -w, not from parsing the status line.
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

if [[ $CASE == "basic" || $CASE == "all" ]]; then
    check "GET /"               404 "$BASE/"
    check "GET unknown (404)"   404 "$BASE/api/v2/does-not-exist"
    check "POST no auth (401)"  401 -X POST -H 'Content-Type: application/json' -d '{}' "$BASE/api/v2/clients/users/read"
    check "preflight PUT"       204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: PUT' -H 'Access-Control-Request-Headers: content-type,authorization' "$BASE/api/v2/clients/users/update"
    check "preflight DELETE"    204 -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: DELETE' "$BASE/api/v2/clients/users/delete"
    check "unsupported method"  405 -X PATCH "$BASE/"
    check "HEAD /"              200 -I "$BASE/api/v2/hello"
    check "GET with Origin"     200 -H 'Origin: http://localhost:3000' "$BASE/api/v2/hello"

    # Allow-Methods check
    headers=""
    status=""
    fetch headers status -X OPTIONS -H 'Access-Control-Request-Method: PUT' "$BASE/api/v2/clients/users/update" || true
    methods="$(printf '%s\n' "$headers" | grep -i '^access-control-allow-methods:' || true)"
    if [[ $methods == *PUT* && $methods == *DELETE* ]]; then
        record_pass "preflight Allow-Methods contains PUT and DELETE"
    else
        record_fail "preflight Allow-Methods missing PUT/DELETE"
    fi
fi

if [[ $CASE == "403" || $CASE == "all" ]]; then
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

if [[ $CASE == "503" || $CASE == "all" ]]; then
    # Banned (503): many identical requests from one IP.
    BAN_COUNT="${BAN_COUNT:-0}"
    if [[ $BAN_COUNT -le 0 ]]; then
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

if [[ $CASE == "429" || $CASE == "all" ]]; then
    # Rate limited (429): many distinct requests from one IP.
    COUNT="${COUNT:-120}"
    echo "note: 429 case assumes the default MAX_FPS=100 (defaults.hpp); COUNT=$COUNT; .env may override these thresholds"
    code=""
    last_headers=""
    for i in $(seq 1 "$COUNT"); do
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

printf '\n%d passed, %d failed, %d skipped\n' "$passed" "$fail" "$skipped"
echo "warning: this client IP may now be rate-limited or banned for the configured duration"

if [[ $fail -gt 0 ]]; then
    exit 1
fi
exit 0

