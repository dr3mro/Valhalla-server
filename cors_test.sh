#!/usr/bin/env bash
# Wire check for CORS headers: every CORS header must appear exactly once per response.
# Usage: ./cors_test.sh [base_url]   (default http://localhost:8080)
# Env: COUNT=N (requests for the 429 case, default 120), BAN_COUNT=N (requests for the 503 case, default 0 = skip)
set -euo pipefail
BASE="${1:-http://localhost:8080}"
fail=0

count() { printf '%s\n' "$1" | grep -ic "^$2:" || true; }

status_of() { printf '%s' "$1" | sed -n '1s/^HTTP\/[^ ]* \([0-9][0-9][0-9]\).*/\1/p' | head -1; }

check() { # check <label> <curl args...>
	local label="$1"
	shift
	local out
	out="$(curl -si --max-time 10 "$@")" || { printf 'FAIL %-28s curl error\n' "$label"; fail=1; return; }
	for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
		local n
		n="$(count "$out" "$h")"
		if [[ $n -ne 1 ]]; then printf 'FAIL %-28s %s appears %s times\n' "$label" "$h" "$n"; fail=1; fi
	done
	printf 'ok   %s\n' "$label"
}

check "GET /"               "$BASE/"
check "GET unknown (404)"   "$BASE/api/v2/does-not-exist"
check "POST no auth (401)"  -X POST -H 'Content-Type: application/json' -d '{}' "$BASE/api/v2/clients/users/read"
check "preflight PUT"       -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: PUT' -H 'Access-Control-Request-Headers: content-type,authorization' "$BASE/api/v2/clients/users/update"
check "preflight DELETE"    -X OPTIONS -H 'Origin: http://localhost:3000' -H 'Access-Control-Request-Method: DELETE' "$BASE/api/v2/clients/users/delete"
check "unsupported method"  -X PATCH "$BASE/"
check "HEAD /"              -I "$BASE/api/v2/hello"
check "GET with Origin"     -H 'Origin: http://localhost:3000' "$BASE/"

# Rate limited (429): many distinct requests from one IP.
COUNT="${COUNT:-120}"
code=""
last=""
for i in $(seq 1 "$COUNT"); do
	last="$(curl -si --max-time 10 -H "X-Test-Id: $i" "$BASE/api/v2/hello")" || true
	code="$(status_of "$last")"
	if [[ $code == "429" ]]; then break; fi
done
if [[ $code != "429" ]]; then
	printf 'SKIP rate limited (429): not provoked after %s requests (last status %s)\n' "$COUNT" "${code:-unknown}"
else
	for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
		n="$(count "$last" "$h")"
		if [[ $n -ne 1 ]]; then printf 'FAIL rate limited (429) %s appears %s times\n' "$h" "$n"; fail=1; fi
	done
	printf 'ok   rate limited (429)\n'
fi

# Banned (503): many identical requests from one IP.
BAN_COUNT="${BAN_COUNT:-0}"
if [[ $BAN_COUNT -le 0 ]]; then
	printf 'SKIP banned (503): set BAN_COUNT=10001 (or higher) to provoke\n'
else
	code=""
	last=""
	for i in $(seq 1 "$BAN_COUNT"); do
		last="$(curl -si --max-time 10 "$BASE/api/v2/hello")" || true
		code="$(status_of "$last")"
		if [[ $code == "503" ]]; then break; fi
	done
	if [[ $code != "503" ]]; then
		printf 'SKIP banned (503): not provoked after %s requests (last status %s)\n' "$BAN_COUNT" "${code:-unknown}"
	else
		for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
			n="$(count "$last" "$h")"
			if [[ $n -ne 1 ]]; then printf 'FAIL banned (503) %s appears %s times\n' "$h" "$n"; fail=1; fi
		done
		printf 'ok   banned (503)\n'
	fi
fi

# Blacklisted (403): only meaningful if the server was started with a matching BLACKLIST entry.
code=""
last="$(curl -si --max-time 10 "$BASE/api/v2/hello")" || true
code="$(status_of "$last")"
if [[ $code == "403" ]]; then
	for h in access-control-allow-origin access-control-allow-methods access-control-allow-headers access-control-expose-headers; do
		n="$(count "$last" "$h")"
		if [[ $n -ne 1 ]]; then printf 'FAIL blacklisted (403) %s appears %s times\n' "$h" "$n"; fail=1; fi
	done
	printf 'ok   blacklisted (403)\n'
else
	printf 'SKIP blacklisted (403): server not blacklisting this client (last status %s); set BLACKLIST to include the client IP to test\n' "${code:-unknown}"
fi

methods="$(curl -si -X OPTIONS -H 'Access-Control-Request-Method: PUT' "$BASE/api/v2/clients/users/update" | grep -i '^access-control-allow-methods:' || true)"
[[ $methods == *PUT* && $methods == *DELETE* ]] || { echo "FAIL preflight Allow-Methods missing PUT/DELETE"; fail=1; }

exit "$fail"