#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file sqlmap_test.sh
#  * @brief Sweep every API endpoint with sqlmap using captured requests from the full live test flow.
#  * @details This script runs a SQL-injection scan over every API endpoint with sqlmap.
#  * It first replays the full live test flow to capture real requests, one file per step.
#  * Then it points sqlmap at each captured request and labels the result pass, fail or needs review.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the deep SQL-injection security sweep for the API.
#  * A security-minded developer runs it against a server they own, usually before a release.
#  * Without it injection flaws in less common endpoints could reach production unnoticed.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - sqlmap: https://sqlmap.org/
#  * - The curl command: https://curl.se/docs/manpage.html
#  * - Bash while loops: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# SQL-injection sweep of every Valhalla-server API endpoint, using sqlmap.
#
# 1. tests/api_tests/sqlmap_gen.py runs the SAME 89-step test flow as test_api.sh/the docs page's benchmark
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
#          PAYLOAD_DIR (default tests/api_tests/payloads next to this script)
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
# No `-e`: each endpoint is classified PASS/FAIL/CHECK and the scan loop continues to the next one.
set -uo pipefail  # Treat unset variables and failed pipeline steps as errors.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # Absolute repository root, found from this file's own location.
# All knobs are env-overridable; defaults target the local server and keep results under build/testing.
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"  # Server address to scan; defaults to a local server.
PAYLOAD_DIR="${PAYLOAD_DIR:-$ROOT/tests/api_tests/payloads}"  # Folder of JSON templates used by the request-capture generator.
OUT_DIR="${OUT_DIR:-$ROOT/build/testing/sqlmap-results}"  # Folder for captured requests, logs and results.
LEVEL="${LEVEL:-1}"  # sqlmap's depth of testing; 1 keeps the full sweep quick.
RISK="${RISK:-1}"  # sqlmap's risk level; 1 is the safe default for a sweep.
SQLMAP_BIN="${SQLMAP_BIN:-sqlmap}"  # Name of the sqlmap command to run.
EXTRA_ARGS="${EXTRA_ARGS:-}"  # Extra words appended to every sqlmap call.
ONLY="${ONLY:-}"  # Optional list of step ids to scan instead of all of them.
SKIP_SQLMAP="${SKIP_SQLMAP:-0}"  # When 1, only capture requests and do not run sqlmap.
GEN="$ROOT/tests/api_tests/sqlmap_gen.py"  # Full path to the request-capture generator.

command -v python3 >/dev/null || { echo "error: 'python3' is required but was not found on PATH" >&2; exit 2; }  # Stop with a clear message if Python is missing.
[[ -f $GEN ]] || { echo "error: $GEN not found" >&2; exit 2; }  # Stop if the capture generator is missing.
[[ -d $PAYLOAD_DIR ]] || { echo "error: payload directory not found: $PAYLOAD_DIR" >&2; exit 2; }  # Stop if the templates folder is missing.
# sqlmap is only required when actually scanning; SKIP_SQLMAP=1 still captures request snapshots.
if [[ $SKIP_SQLMAP != 1 ]]; then  # Only require sqlmap when we will actually scan.
	command -v "$SQLMAP_BIN" >/dev/null || { echo "error: '$SQLMAP_BIN' is required (or set SKIP_SQLMAP=1) but was not found on PATH" >&2; exit 2; }  # Stop early if sqlmap is not installed and scanning was requested.
fi

# Colour only on a terminal; logs and CI output stay plain text.
if [[ -t 1 ]]; then G=$'\033[32m'; R=$'\033[31m'; Y=$'\033[33m'; B=$'\033[1m'; N=$'\033[0m'; else G=""; R=""; Y=""; B=""; N=""; fi
# @brief Print a plain line.
# @param $* Text to print.
say()  { printf '%s\n' "$*"; }
# @brief Print a yellow [*] informational line.
# @param $* Text to print.
note() { printf '%s[*]%s %s\n' "$Y" "$N" "$*"; }
# @brief Print a green [+] success line.
# @param $* Text to print.
ok()   { printf '%s[+]%s %s\n' "$G" "$N" "$*"; }
# @brief Print a red [-] error line to stderr.
# @param $* Text to print.
err()  { printf '%s[-]%s %s\n' "$R" "$N" "$*" >&2; }

note "Base URL: $BASE_URL"
note "Payloads: $PAYLOAD_DIR"
note "Results:  $OUT_DIR"
echo

# Drop stale snapshots so the manifest can only reference files captured by this run.
rm -rf "$OUT_DIR/requests"  # Delete old captures so nothing stale is scanned.
mkdir -p "$OUT_DIR"  # Make sure the results folder exists.

# ---------------------------------------------------------------------------
# 1. Run the live flow and capture one request file per step (all 89).
# ---------------------------------------------------------------------------
say "${B}== Running the live test flow against $BASE_URL ==${N}"
# Capture the generator's status instead of aborting: partial captures are still useful for a targeted ONLY run.
GEN_STATUS=0  # Start by assuming the capture generator will succeed.
python3 "$GEN" "$PAYLOAD_DIR" "$BASE_URL" "$OUT_DIR" || GEN_STATUS=$?  # Replay the live flow and save its status instead of aborting on partial captures.
MANIFEST="$OUT_DIR/manifest.tsv"  # File listing every captured request by id.
if [[ ! -s $MANIFEST ]]; then  # An empty manifest means nothing was captured, so stop.
	err "No endpoints were captured (manifest is empty). Is the server running at $BASE_URL ?"
	err "  check with: curl $BASE_URL/hello"
	exit 2
fi
if ((GEN_STATUS != 0)); then  # Warn that some steps did not get their expected result.
	note "Some steps in the live flow did not get their expected status (see above) - continuing anyway; those endpoints just won't have a fresh snapshot, or will test with stale/incomplete data."
fi
echo

if [[ $SKIP_SQLMAP == 1 ]]; then  # In capture-only mode, report and exit here.
	ok "SKIP_SQLMAP=1: captured $(wc -l <"$MANIFEST" | tr -d ' ') request files under $OUT_DIR/requests, not running sqlmap."
	exit 0  # Return success when no injection was found.
fi

# ---------------------------------------------------------------------------
# 2. Run sqlmap against every captured endpoint (or the ones named in $ONLY).
# ---------------------------------------------------------------------------
# One log per endpoint plus a running verdict summary; truncate the summary from any earlier run.
LOG_DIR="$OUT_DIR/logs"  # Folder for one sqlmap log per endpoint.
mkdir -p "$LOG_DIR"  # Make sure the log folder exists.
SUMMARY="$OUT_DIR/summary.txt"  # Running table of verdicts.
: >"$SUMMARY"  # Empty the summary from any earlier run.

say "${B}== Scanning with sqlmap (--level=$LEVEL --risk=$RISK${EXTRA_ARGS:+ $EXTRA_ARGS}) ==${N}"
TOTAL=0  # Count how many endpoints we scan.
# The full live flow already ran above; ONLY just filters which captured requests sqlmap is pointed at.
while IFS=$'\t' read -r sid method path; do  # Read the manifest one tab-separated row at a time.
	if [[ -n $ONLY ]]; then  # If ONLY was set, skip ids that are not in it.
		case " $ONLY " in *" $sid "*) ;; *) continue ;; esac  # A space-padded match makes sure we match a whole id, not part of one.
	fi
	TOTAL=$((TOTAL + 1))  # Count this endpoint before scanning it.
	req="$OUT_DIR/requests/${sid}.req"  # The captured request file for this step.
	log="$LOG_DIR/${sid}.log"  # Where this step's sqlmap log will go.
	note "Testing ${B}${sid}${N}  ($method $path)..."  # Show which endpoint is being scanned now.
	# --batch keeps sqlmap non-interactive; --ignore-code skips auth/rate-limit responses, not SQL findings.
	# The payloads are intentionally malicious - only scan a server you own or are authorised to test.
	# shellcheck disable=SC2086
	# NOTE: the sqlmap command continues on the next line; the flags there finish the invocation.
	"$SQLMAP_BIN" -r "$req" --batch --level="$LEVEL" --risk="$RISK" --dbms=PostgreSQL \
		--ignore-code=401,403,429,503 $EXTRA_ARGS >"$log" 2>&1  # Skip auth and rate-limit responses; save all output to the log.
# Classify by log text: an explicit "vulnerable" fails, "not injectable" passes, anything else needs review.
	if grep -qi "is vulnerable" "$log"; then  # A clear 'vulnerable' message means a real finding.
		verdict="FAIL"  # Mark this endpoint as a confirmed problem.
		err "$sid: VULNERABLE - see $log"  # Print a loud error for a confirmed injection.
		grep -B1 -A2 "^Parameter:" "$log" | sed 's/^/      /'  # Show a little context around each affected parameter.
	elif grep -qi "does not seem to be injectable\|do not appear to be injectable\|all tested parameters" "$log"; then  # These phrases mean sqlmap found no injection.
		verdict="PASS"  # Mark this endpoint as clean.
		ok "$sid: not injectable"  # Print a success line for a clean endpoint.
	else
		verdict="CHECK"  # Anything else is inconclusive and needs a human to look.
		printf '%s[?]%s %s: inconclusive - review %s\n' "$Y" "$N" "$sid" "$log"  # Show the inconclusive result with its log path.
	fi
	printf '%-7s %-28s %-7s %s\n' "$verdict" "$sid" "$method" "$path" >>"$SUMMARY"  # Append one aligned row to the summary table.
done <"$MANIFEST"  # Continue until every manifest row has been scanned.

echo
say "${B}== Summary ($TOTAL endpoint(s) tested) ==${N}"  # Show the headline count of scanned endpoints.
# Re-read the summary to colour the table and count failures/inconclusive results for the exit status.
FAIL_COUNT=0  # Count confirmed vulnerabilities for the exit status.
CHECK_COUNT=0  # Count inconclusive results for the closing note.
while read -r verdict sid method path; do  # Read the summary back to print it in colour.
	case $verdict in  # Colour and count each row according to its verdict.
	PASS)  printf '  %s%-6s%s %-28s %s %s\n' "$G" "$verdict" "$N" "$sid" "$method" "$path" ;;
	FAIL)  printf '  %s%-6s%s %-28s %s %s\n' "$R" "$verdict" "$N" "$sid" "$method" "$path"; FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
	*)     printf '  %s%-6s%s %-28s %s %s\n' "$Y" "$verdict" "$N" "$sid" "$method" "$path"; CHECK_COUNT=$((CHECK_COUNT + 1)) ;;
	esac
done <"$SUMMARY"  # Stop when the whole summary has been printed.
echo
say "Full logs in:      $LOG_DIR"
say "Request files in:  $OUT_DIR/requests"
say "Note: nested JSON fields (objects/arrays inside a body) are not auto-tested - see the header comment."

# A confirmed injection fails the run; inconclusive results are reported but do not change the exit status.
if ((FAIL_COUNT > 0)); then  # Any confirmed injection fails the whole run.
	err "$FAIL_COUNT endpoint(s) reported an injectable parameter. Review the logs above before deploying."  # Tell the caller to investigate before shipping.
	exit 1  # Return failure so CI can block the release.
fi
((CHECK_COUNT > 0)) && note "$CHECK_COUNT endpoint(s) were inconclusive; review their logs."  # Inconclusive results are reported but do not fail the run.
ok "No injectable parameters found."  # Report a fully clean scan.
exit 0
