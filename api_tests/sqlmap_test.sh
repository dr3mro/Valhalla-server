#!/usr/bin/env bash
# SQL-injection sweep of Valhalla-server's API using sqlmap.
#
# Creates one throwaway provider account, then runs sqlmap against:
#   - POST /clients/providers/login   (username, password)      - unauthenticated
#   - POST /clients/users/login       (username, password)      - unauthenticated
#   - POST /clients/providers/read    (id)                       - authenticated
#   - POST /clients/providers/search  (keyword, filter, order_by) - authenticated; order_by is the
#       interesting one: if it's ever interpolated raw into an ORDER BY clause instead of being
#       validated against a column allow-list, that's a classic injection point normal WHERE-clause
#       fuzzing won't find.
#
# Each target is a raw HTTP request file under $OUT_DIR/requests/, and each sqlmap run's full output
# goes to $OUT_DIR/logs/. A one-line PASS/FAIL summary per parameter is printed at the end and written
# to $OUT_DIR/summary.txt. "FAIL" means sqlmap reported the parameter as vulnerable - take it seriously.
#
# Usage:   ./api_tests/sqlmap_test.sh
# Env:     BASE_URL (default http://127.0.0.1:8080/api/v2)
#          OUT_DIR   (default api_tests/sqlmap-results, next to this script)
#          LEVEL (default 3) RISK (default 2)   - sqlmap's --level / --risk
#          SQLMAP_BIN (default sqlmap)
#          EXTRA_ARGS  - extra args appended to every sqlmap invocation, e.g. EXTRA_ARGS='--tamper=space2comment'
#          CLEANUP=0   - keep the throwaway provider account instead of deleting it afterwards
#          KEEP_REQ=1  - keep the generated .req files after the run (they're kept by default; set to 0 to delete)
# Needs:   curl, jq, python3, sqlmap.
#
# IMPORTANT: only ever point this at a server you own or have explicit permission to test. sqlmap
# sends intentionally malicious payloads; running it against someone else's deployment without
# consent is illegal in most places.
#
# The server's DoS filter (MAX_REQUESTS / PERIOD, default 10000 req / 30s) will start throttling or
# banning sqlmap's traffic on a thorough scan. If runs start failing with 429/ban responses partway
# through, either raise MAX_REQUESTS/PERIOD/BAN_DURATION in the server's .env for the duration of the
# test, or add your test machine's IP to WHITELIST, then restart the server.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"
OUT_DIR="${OUT_DIR:-$ROOT/sqlmap-results}"
LEVEL="${LEVEL:-3}"
RISK="${RISK:-2}"
SQLMAP_BIN="${SQLMAP_BIN:-sqlmap}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
CLEANUP="${CLEANUP:-1}"
KEEP_REQ="${KEEP_REQ:-1}"
RUN="$(date +%s | tail -c 7)"
PROVIDER_USERNAME="sqlmap${RUN}"
PROVIDER_PASSWORD='1234@Abc!'

for tool in curl jq python3 "$SQLMAP_BIN"; do
	command -v "$tool" >/dev/null || { echo "error: '$tool' is required but was not found on PATH" >&2; exit 2; }
done

REQ_DIR="$OUT_DIR/requests"
LOG_DIR="$OUT_DIR/logs"
mkdir -p "$REQ_DIR" "$LOG_DIR"
SUMMARY="$OUT_DIR/summary.txt"
: >"$SUMMARY"

if [[ -t 1 ]]; then G=$'\033[32m'; R=$'\033[31m'; Y=$'\033[33m'; B=$'\033[1m'; N=$'\033[0m'; else G=""; R=""; Y=""; B=""; N=""; fi

say()  { printf '%s\n' "$*"; }
note() { printf '%s[*]%s %s\n' "$Y" "$N" "$*"; }
ok()   { printf '%s[+]%s %s\n' "$G" "$N" "$*"; }
err()  { printf '%s[-]%s %s\n' "$R" "$N" "$*" >&2; }

note "Base URL: $BASE_URL"
note "Results:  $OUT_DIR"
note "sqlmap:   --level=$LEVEL --risk=$RISK${EXTRA_ARGS:+ $EXTRA_ARGS}"
echo

# ---------------------------------------------------------------------------
# 1. Throwaway provider account: gives us a real login (200, not 401) and a
#    token for the authenticated endpoints below.
# ---------------------------------------------------------------------------
note "Creating a throwaway provider account ($PROVIDER_USERNAME)..."
CREATE_BODY="$(python3 - "$PROVIDER_USERNAME" "$PROVIDER_PASSWORD" "$RUN" <<'PY'
import json, sys
username, password, run = sys.argv[1:4]
print(json.dumps({
    "avatar": "http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",
    "active": True, "language": "en", "username": username, "password": password,
    "fullname": "SQLMap Test", "phone": "+201299999999", "email": f"{username}@mail.com",
    "country": "Egypt", "city": "Damietta", "address": "test street", "dob": "17-02-1985",
    "gender": "Male", "position": "Doctor", "speciality": "Cardiology",
}))
PY
)"
CREATE_RESP="$(curl -sS -X POST "$BASE_URL/clients/providers/create" -H 'Content-Type: application/json' -d "$CREATE_BODY")"
PROVIDER_ID="$(jq -r '.id // empty' <<<"$CREATE_RESP")"
if [[ -z $PROVIDER_ID ]]; then
	err "Could not create the test provider account. Response: $CREATE_RESP"
	err "Is the server running at $BASE_URL ? (check with: curl $BASE_URL/hello)"
	exit 2
fi
ok "Created provider id=$PROVIDER_ID"

LOGIN_RESP="$(curl -sS -X POST "$BASE_URL/clients/providers/login" -H 'Content-Type: application/json' \
	-d "{\"username\":\"$PROVIDER_USERNAME\",\"password\":\"$PROVIDER_PASSWORD\"}")"
PROVIDER_TOKEN="$(jq -r '.token // empty' <<<"$LOGIN_RESP")"
if [[ -z $PROVIDER_TOKEN ]]; then
	err "Could not log in with the test provider account. Response: $LOGIN_RESP"
	exit 2
fi
ok "Logged in, got a token"
echo

cleanup_account() {
	[[ $CLEANUP == 1 && -n $PROVIDER_TOKEN ]] || return 0
	note "Cleaning up the throwaway provider account..."
	curl -sS -X DELETE "$BASE_URL/clients/providers/delete?id=$PROVIDER_ID" \
		-H "Authorization: Bearer $PROVIDER_TOKEN" >/dev/null
}
trap cleanup_account EXIT

# ---------------------------------------------------------------------------
# 2. Build one raw-HTTP request file per target. sqlmap reads these with -r.
#    A '*' marks the field(s) sqlmap should fuzz; every other field keeps its
#    real (valid) value so the baseline response is the normal, successful one.
# ---------------------------------------------------------------------------
write_req() {
	# write_req <file> <method> <path> [Authorization-token] <<< body
	local file=$1 method=$2 path=$3 token=${4:-}
	local body; body="$(cat)"
	{
		printf '%s %s HTTP/1.1\r\n' "$method" "$path"
		printf 'Host: %s\r\n' "$HOST_ONLY"
		printf 'Content-Type: application/json\r\n'
		[[ -n $token ]] && printf 'Authorization: Bearer %s\r\n' "$token"
		printf '\r\n'
		printf '%s' "$body"
	} >"$file"
}

HOST_PATH="${BASE_URL#http*://}"
HOST_ONLY="${HOST_PATH%%/*}"
API_PREFIX="/${HOST_PATH#*/}"
[[ $API_PREFIX == "$HOST_PATH" ]] && API_PREFIX=""   # no path component in BASE_URL

write_req "$REQ_DIR/providers-login.req" POST "${API_PREFIX}/clients/providers/login" <<EOF
{"username":"${PROVIDER_USERNAME}*","password":"${PROVIDER_PASSWORD}"}
EOF

write_req "$REQ_DIR/users-login.req" POST "${API_PREFIX}/clients/users/login" <<EOF
{"username":"nonexistent-user*","password":"whatever"}
EOF

write_req "$REQ_DIR/providers-read.req" POST "${API_PREFIX}/clients/providers/read" "$PROVIDER_TOKEN" <<EOF
{"id":$PROVIDER_ID*,"schema":[]}
EOF

write_req "$REQ_DIR/providers-search.req" POST "${API_PREFIX}/clients/providers/search" "$PROVIDER_TOKEN" <<EOF
{"keyword":"cardiology*","filter":"speciality","limit":5,"offset":0,"order_by":"id*","direction":0}
EOF

# ---------------------------------------------------------------------------
# 3. Run sqlmap against each. --ignore-code is set per target to the status
#    the server legitimately returns for a *wrong* (but not malicious) value,
#    so sqlmap doesn't abort thinking the baseline request itself is broken.
# ---------------------------------------------------------------------------
run_target() {
	local name=$1 req=$2 param=$3 ignore=${4:-}
	local log="$LOG_DIR/${name}.log"
	note "Testing ${B}${name}${N} (parameter: ${param})..."
	# shellcheck disable=SC2086
	"$SQLMAP_BIN" -r "$req" --batch --level="$LEVEL" --risk="$RISK" -p "$param" --dbms=PostgreSQL \
		${ignore:+--ignore-code=$ignore} $EXTRA_ARGS >"$log" 2>&1
	local verdict
	if grep -q "Parameter: ${param}" "$log" && grep -qi "is vulnerable" "$log"; then
		verdict="FAIL"
		err "${name} / ${param}: VULNERABLE - see $log"
		grep -A2 "Parameter: ${param}" "$log" | sed 's/^/      /'
	elif grep -qi "does not seem to be injectable" "$log" || grep -qi "all tested parameters do not appear" "$log"; then
		verdict="PASS"
		ok "${name} / ${param}: not injectable"
	else
		verdict="CHECK"
		printf '%s[?]%s %s\n' "$Y" "$N" "${name} / ${param}: inconclusive - review $log"
	fi
	printf '%-8s %-22s %s\n' "$verdict" "$name" "$param" >>"$SUMMARY"
}

echo
run_target providers-login "$REQ_DIR/providers-login.req" username 401
run_target users-login     "$REQ_DIR/users-login.req"     username 401
run_target providers-read   "$REQ_DIR/providers-read.req"   id
run_target providers-search-keyword  "$REQ_DIR/providers-search.req" keyword
run_target providers-search-order_by "$REQ_DIR/providers-search.req" order_by

[[ $KEEP_REQ == 0 ]] && rm -rf "$REQ_DIR"

# ---------------------------------------------------------------------------
# 4. Summary
# ---------------------------------------------------------------------------
echo
say "${B}== Summary ==${N}"
FAIL_COUNT=0
while read -r verdict name param; do
	case $verdict in
	PASS)  printf '  %s%-6s%s %s / %s\n' "$G" "$verdict" "$N" "$name" "$param" ;;
	FAIL)  printf '  %s%-6s%s %s / %s\n' "$R" "$verdict" "$N" "$name" "$param"; FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
	*)     printf '  %s%-6s%s %s / %s\n' "$Y" "$verdict" "$N" "$name" "$param" ;;
	esac
done <"$SUMMARY"
echo
say "Full logs in: $LOG_DIR"
say "Request files in: $REQ_DIR"

if ((FAIL_COUNT > 0)); then
	err "$FAIL_COUNT parameter(s) reported as injectable. Review the logs above before deploying."
	exit 1
fi
ok "No injectable parameters found."
exit 0
