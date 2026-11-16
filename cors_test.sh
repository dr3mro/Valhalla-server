#!/usr/bin/env bash
# Wire check for CORS headers: every CORS header must appear exactly once per response.
# Usage: ./cors_test.sh [base_url]   (default http://localhost:8080)
set -euo pipefail
BASE="${1:-http://localhost:8080}"
fail=0

count() { printf '%s\n' "$1" | grep -ic "^$2:" || true; }

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

methods="$(curl -si -X OPTIONS -H 'Access-Control-Request-Method: PUT' "$BASE/api/v2/clients/users/update" | grep -i '^access-control-allow-methods:' || true)"
[[ $methods == *PUT* && $methods == *DELETE* ]] || { echo "FAIL preflight Allow-Methods missing PUT/DELETE"; fail=1; }

exit "$fail"
