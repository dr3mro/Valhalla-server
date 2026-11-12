#!/usr/bin/env bash
# SQL-injection sweep of every Valhalla-server API endpoint, using sqlmap.
#
# 1. api_tests/sqlmap_gen.py runs the SAME 89-step test flow as test_api.sh/the docs page's benchmark
#    panel against the live server (provider/user -> services -> patient -> health/drugs/reports ->
#    visit -> requests/prescriptions/paidservices -> appointment -> suspend/activate -> clean-up) and
#    snapshots a raw-HTTP request file for EVERY one of those 89 steps (one per step id, even when
#    several steps share the same method+path - e.g. each role's create/read/update/search gets its own
#    file) with real ids and tokens already filled in - so sqlmap gets a valid, successful baseline for
#    each step instead of a 400/401 it would otherwise abort on.
# 2. This script then runs sqlmap against every one of those 89 request files and reports which
#    parameters, if any, come back vulnerable.
#
# sqlmap auto-detects every top-level JSON body key and query parameter as testable - no '*' markers are
# added. That means NESTED fields (the "examination" object on a visit, "staff" on a clinic, items inside
# a "drugs"/"reports"/"requests" array, ...) are NOT covered by a default run. To fuzz one of those, open
# its .req file under $OUT_DIR/requests/, put a '*' right after the value to mutate, and run
# `sqlmap -r that.req --batch --level=$LEVEL --risk=$RISK --dbms=PostgreSQL` yourself.
#
# The suspend/activate steps ARE included and run for real this time, but only against throwaway
# provider/user accounts this run creates itself - never a real account - so permanently suspending them
# is harmless.
#
# Usage:   ./sqlmap_test.sh
# Env:     BASE_URL (default http://127.0.0.1:8080/api/v2)
#          PAYLOAD_DIR (default api_tests/payloads next to this script)
#          OUT_DIR   (default sqlmap-results, next to this script)
#          LEVEL (default 1) RISK (default 1)   - sqlmap's --level / --risk. The default sweep of all 89
#            steps is meant to be quick; raise these (e.g. LEVEL=3 RISK=2, or even 5/3) for a
#            thorough pass on one endpoint via ONLY='<step-id>' once something looks interesting.
#          SQLMAP_BIN (default sqlmap)
#          EXTRA_ARGS  - extra args appended to every sqlmap invocation, e.g. EXTRA_ARGS='--tamper=space2comment'
#          ONLY='<step-id> <step-id> ...'  - only run sqlmap against these endpoints (ids come from the
#            left column the generator prints, or $OUT_DIR/manifest.tsv); the full live-flow still runs
#            first, so ids/tokens/clean-up all still happen.
#          SKIP_SQLMAP=1  - only run the live flow and capture requests; don't invoke sqlmap at all
# Needs:   python3, sqlmap (unless SKIP_SQLMAP=1).
#
# IMPORTANT: only ever point this at a server you own or have explicit permission to test. sqlmap sends
# intentionally malicious payloads; running it against someone else's deployment without consent is
# illegal in most places. A full sweep also creates and deletes real records (providers, users, clinics,
# patients, visits, appointments, ...) using throwaway data unique to this run.
#
# The server's DoS filter (MAX_REQUESTS / PERIOD, default 10000 req / 30s) will start throttling or
# banning this traffic on a thorough scan across all 89 steps. If runs start failing with 429/ban
# responses partway through, either raise MAX_REQUESTS/PERIOD/BAN_DURATION in the server's .env for the
# duration of the test, or add your test machine's IP to WHITELIST, then restart the server.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"
PAYLOAD_DIR="${PAYLOAD_DIR:-$ROOT/api_tests/payloads}"
OUT_DIR="${OUT_DIR:-$ROOT/sqlmap-results}"
LEVEL="${LEVEL:-1}"
RISK="${RISK:-1}"
SQLMAP_BIN="${SQLMAP_BIN:-sqlmap}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
ONLY="${ONLY:-}"
SKIP_SQLMAP="${SKIP_SQLMAP:-0}"
GEN="$ROOT/api_tests/sqlmap_gen.py"

command -v python3 >/dev/null || { echo "error: 'python3' is required but was not found on PATH" >&2; exit 2; }
[[ -f $GEN ]] || { echo "error: $GEN not found" >&2; exit 2; }
[[ -d $PAYLOAD_DIR ]] || { echo "error: payload directory not found: $PAYLOAD_DIR" >&2; exit 2; }
if [[ $SKIP_SQLMAP != 1 ]]; then
	command -v "$SQLMAP_BIN" >/dev/null || { echo "error: '$SQLMAP_BIN' is required (or set SKIP_SQLMAP=1) but was not found on PATH" >&2; exit 2; }
fi

if [[ -t 1 ]]; then G=$'\033[32m'; R=$'\033[31m'; Y=$'\033[33m'; B=$'\033[1m'; N=$'\033[0m'; else G=""; R=""; Y=""; B=""; N=""; fi
say()  { printf '%s\n' "$*"; }
note() { printf '%s[*]%s %s\n' "$Y" "$N" "$*"; }
ok()   { printf '%s[+]%s %s\n' "$G" "$N" "$*"; }
err()  { printf '%s[-]%s %s\n' "$R" "$N" "$*" >&2; }

note "Base URL: $BASE_URL"
note "Payloads: $PAYLOAD_DIR"
note "Results:  $OUT_DIR"
echo

rm -rf "$OUT_DIR/requests"
mkdir -p "$OUT_DIR"

# ---------------------------------------------------------------------------
# 1. Run the live flow and capture one request file per step (all 89).
# ---------------------------------------------------------------------------
say "${B}== Running the live test flow against $BASE_URL ==${N}"
GEN_STATUS=0
python3 "$GEN" "$PAYLOAD_DIR" "$BASE_URL" "$OUT_DIR" || GEN_STATUS=$?
MANIFEST="$OUT_DIR/manifest.tsv"
if [[ ! -s $MANIFEST ]]; then
	err "No endpoints were captured (manifest is empty). Is the server running at $BASE_URL ?"
	err "  check with: curl $BASE_URL/hello"
	exit 2
fi
if ((GEN_STATUS != 0)); then
	note "Some steps in the live flow did not get their expected status (see above) - continuing anyway; those endpoints just won't have a fresh snapshot, or will test with stale/incomplete data."
fi
echo

if [[ $SKIP_SQLMAP == 1 ]]; then
	ok "SKIP_SQLMAP=1: captured $(wc -l <"$MANIFEST" | tr -d ' ') request files under $OUT_DIR/requests, not running sqlmap."
	exit 0
fi

# ---------------------------------------------------------------------------
# 2. Run sqlmap against every captured endpoint (or the ones named in $ONLY).
# ---------------------------------------------------------------------------
LOG_DIR="$OUT_DIR/logs"
mkdir -p "$LOG_DIR"
SUMMARY="$OUT_DIR/summary.txt"
: >"$SUMMARY"

say "${B}== Scanning with sqlmap (--level=$LEVEL --risk=$RISK${EXTRA_ARGS:+ $EXTRA_ARGS}) ==${N}"
TOTAL=0
while IFS=$'\t' read -r sid method path; do
	if [[ -n $ONLY ]]; then
		case " $ONLY " in *" $sid "*) ;; *) continue ;; esac
	fi
	TOTAL=$((TOTAL + 1))
	req="$OUT_DIR/requests/${sid}.req"
	log="$LOG_DIR/${sid}.log"
	note "Testing ${B}${sid}${N}  ($method $path)..."
	# shellcheck disable=SC2086
	"$SQLMAP_BIN" -r "$req" --batch --level="$LEVEL" --risk="$RISK" --dbms=PostgreSQL \
		--ignore-code=401,403,429,503 $EXTRA_ARGS >"$log" 2>&1
	if grep -qi "is vulnerable" "$log"; then
		verdict="FAIL"
		err "$sid: VULNERABLE - see $log"
		grep -B1 -A2 "^Parameter:" "$log" | sed 's/^/      /'
	elif grep -qi "does not seem to be injectable\|do not appear to be injectable\|all tested parameters" "$log"; then
		verdict="PASS"
		ok "$sid: not injectable"
	else
		verdict="CHECK"
		printf '%s[?]%s %s: inconclusive - review %s\n' "$Y" "$N" "$sid" "$log"
	fi
	printf '%-7s %-28s %-7s %s\n' "$verdict" "$sid" "$method" "$path" >>"$SUMMARY"
done <"$MANIFEST"

echo
say "${B}== Summary ($TOTAL endpoint(s) tested) ==${N}"
FAIL_COUNT=0
CHECK_COUNT=0
while read -r verdict sid method path; do
	case $verdict in
	PASS)  printf '  %s%-6s%s %-28s %s %s\n' "$G" "$verdict" "$N" "$sid" "$method" "$path" ;;
	FAIL)  printf '  %s%-6s%s %-28s %s %s\n' "$R" "$verdict" "$N" "$sid" "$method" "$path"; FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
	*)     printf '  %s%-6s%s %-28s %s %s\n' "$Y" "$verdict" "$N" "$sid" "$method" "$path"; CHECK_COUNT=$((CHECK_COUNT + 1)) ;;
	esac
done <"$SUMMARY"
echo
say "Full logs in:      $LOG_DIR"
say "Request files in:  $OUT_DIR/requests"
say "Note: nested JSON fields (objects/arrays inside a body) are not auto-tested - see the header comment."

if ((FAIL_COUNT > 0)); then
	err "$FAIL_COUNT endpoint(s) reported an injectable parameter. Review the logs above before deploying."
	exit 1
fi
((CHECK_COUNT > 0)) && note "$CHECK_COUNT endpoint(s) were inconclusive; review their logs."
ok "No injectable parameters found."
exit 0
