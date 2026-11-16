#!/usr/bin/env bash
# /**
#  * @file run_all_tests.sh
#  * @brief Orchestrate the local test suite (lint, build, unit, API, fuzz, CORS and opt-in security/perf) against a temporary run4 benchmark server and report a pass/fail summary.
#  * @details Start a temporary benchmark server and run the project's test suites against it, then print a pass/fail summary.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the main local 'run everything' script used before a release or in CI.
#  * It lets developers switch whole suites on or off with flags instead of editing code.
#  * Without it, each test script would have to be started and cleaned up by hand.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - bash trap (cleanup on exit): https://www.gnu.org/software/bash/manual/html_node/Bourne-Shell-Builtins.html
#  * - curl: https://curl.se/docs/manual.html
#  * - CTest: https://cmake.org/cmake/help/latest/manual/ctest.1.html
#  * - POSIX signals: https://www.gnu.org/software/libc/manual/html_node/Signals.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Run the run4 benchmark server configuration together with the project's test suite.
#
# By default this runs the fast, local checks (shell lint, unit tests, API tests,
# fuzz_auth, CORS checks) against a freshly started server. Heavier suites
# (sqlmap, schemathesis, the performance baseline) are opt-in.
#
# Usage:  ./run_all.sh [options]
#   --all            enable every suite (sqlmap, schemathesis, perf smoke)
#   --no-build       skip building (use existing build/ directories)
#   --no-unit        skip Catch2 unit tests
#   --no-lint        skip tools/lint_sh.sh
#   --no-api         skip test_api.sh
#   --no-fuzz        skip fuzz_auth.sh
#   --no-cors        skip cors_test.sh (and cors_react_check.sh)
#   --sqlmap         enable sqlmap_test.sh
#   --schemathesis   enable schemathesis_test.sh
#   --perf           enable tools/perf_baseline.sh (smoke by default)
#   --perf-full      run the full performance baseline (not smoke)
#   --skip-db-init   do not run db_init.sh before starting the server
#   --skip-server    do not start/stop the server (use an already running one)
#   --keep-server    leave the server running afterwards
#   --base-url URL   override BASE_URL (default http://127.0.0.1:8080/api/v2)
#   --port N         server port (default 8080)
#   -h, --help       show this help
# -u and pipefail catch typos and hidden pipeline failures, but -e is deliberately omitted:
# each suite is wrapped in `if ...; then record` so one failing suite does not abort the whole run.
set -uo pipefail  # u = unset variable is an error, pipefail = catch pipeline failures; no -e so every suite can report its own result.

# Resolve the repository root from this script's location so relative paths work from any CWD.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # BASH_SOURCE[0] is this script's path; ../.. climbs to the repository root.
cd "$ROOT" || exit 1  # cd into the root so all later relative paths work.

# Defaults: DO_* flags are 1/0 booleans; an empty BASE_URL means "derive it from PORT" below.
# Defaults
DO_BUILD=1  # 1 = run the build step; 0 = skip it.
DO_UNIT=1  # 1 = run the Catch2 unit tests.
DO_LINT=1  # 1 = run the shell linter.
DO_API=1  # 1 = run the API tests.
DO_FUZZ=1  # 1 = run the fuzz_auth tests.
DO_CORS=1  # 1 = run the CORS tests.
DO_SQLMAP=0  # 1 = run sqlmap; off by default because it is slow and needs extra setup.
DO_SCHEMATHESIS=0  # 1 = run schemathesis; off by default for the same reason.
DO_PERF=0  # 1 = run the performance baseline; off by default.
PERF_FULL=0  # 1 = run the full baseline instead of the quick smoke version.
SKIP_DB_INIT=0  # 1 = skip database initialization before starting the server.
SKIP_SERVER=0  # 1 = use an already running server instead of starting and stopping one.
KEEP_SERVER=0  # 1 = leave the server running after the tests finish.
PORT=8080  # the TCP port the test server will listen on.
BASE_URL=""  # empty means 'derive the URL from PORT further down'.

while [[ $# -gt 0 ]]; do  # the loop continues while there are still command-line arguments.
    case "$1" in  # case selects the matching option name.
        --all) DO_SQLMAP=1; DO_SCHEMATHESIS=1; DO_PERF=1; shift ;;  # --all switches on all three heavy opt-in suites at once.
        --no-build) DO_BUILD=0; shift ;;  # --no-build flips the build flag off.
        --no-unit) DO_UNIT=0; shift ;;
        --no-lint) DO_LINT=0; shift ;;
        --no-api) DO_API=0; shift ;;
        --no-fuzz) DO_FUZZ=0; shift ;;
        --no-cors) DO_CORS=0; shift ;;
        --sqlmap) DO_SQLMAP=1; shift ;;
        --schemathesis) DO_SCHEMATHESIS=1; shift ;;
        --perf) DO_PERF=1; shift ;;
        --perf-full) DO_PERF=1; PERF_FULL=1; shift ;;
        --skip-db-init) SKIP_DB_INIT=1; shift ;;
        --skip-server) SKIP_SERVER=1; shift ;;
        --keep-server) KEEP_SERVER=1; shift ;;
        --base-url) BASE_URL="$2"; shift 2 ;;  # --base-url consumes the next argument as the URL.
        --port) PORT="$2"; shift 2 ;;  # --port consumes the next argument as the port number.
        # Print the usage comment block embedded at the top of this script.
        -h|--help) sed -n '3,32p' "$0"; exit 0 ;;  # sed prints lines 3-32 of THIS file, which are the embedded usage notes; do not change that range.
        *) echo "unknown option: $1" >&2; exit 2 ;;  # anything else is an unknown option and exits with status 2.
    esac
done

# Default the base URL from the chosen port, then export it so child test scripts target the same server.
BASE_URL="${BASE_URL:-http://127.0.0.1:$PORT/api/v2}"  # ${BASE_URL:-...} supplies a default only when the variable is empty.
export BASE_URL  # export makes the URL visible to the test scripts this one launches.

# Accumulators: the failure count drives the exit code; the parallel arrays drive the summary table.
failures=0  # number of failed suites; becomes the final exit code.
SUITE_NAMES=()  # parallel array holding each suite's display name.
SUITE_STATUS=()  # parallel array holding each suite's PASS/FAIL text.

# @brief Record one suite's result, print a colored PASS/FAIL line, and append it to the summary arrays.
# @param $1 suite name (display label).
# @param $2 suite exit status (0 = pass; any other value increments the failure count).
# @return None; updates the globals `failures`, `SUITE_NAMES`, and `SUITE_STATUS`.
record() { # name status
    if [[ $2 -ne 0 ]]; then  # a non-zero status counts as one failed suite.
        echo "${C_R}FAIL:${C_N} $1" >&2  # print failures on stderr, in red when colors are available.
        failures=$((failures + 1))  # add one to the failure count.
        SUITE_STATUS+=("FAIL")  # remember FAIL so the summary can show it.
    else
        echo "${C_G}PASS:${C_N} $1"  # print a successful suite on normal output.
        SUITE_STATUS+=("PASS")  # remember PASS for the summary.
    fi
    SUITE_NAMES+=("$1")  # store the name at the same index as its status.
}

# Only emit ANSI colors when stdout is a terminal; piped/CI output stays plain text.
if [[ -t 1 ]]; then  # -t 1 checks whether stdout is a terminal; pipes and logs stay plain text.
    C_G=$'\033[32m'; C_R=$'\033[31m'; C_B=$'\033[1m'; C_N=$'\033[0m'  # these are ANSI escape codes for green, red, bold and reset.
else
    C_G=""; C_R=""; C_B=""; C_N=""  # empty codes mean 'print no colors'.
fi

# @brief Print the per-suite result table and the total failure count, colorized when available.
# @return None; reads the globals `SUITE_NAMES`, `SUITE_STATUS`, and `failures`.
print_summary() {
    echo "===== summary ====="
    printf '%s%-28s %s%s\n' "$C_B" "Suite" "Result" "$C_N"  # %-28s left-aligns the column name in a fixed-width table.
    printf '%-28s %s\n' "----------------------------" "------"
    if [[ ${#SUITE_NAMES[@]} -gt 0 ]]; then  # print rows only if at least one suite actually ran.
        local i status color  # local keeps these variables inside the function.
        for i in "${!SUITE_NAMES[@]}"; do  # ${!SUITE_NAMES[@]} expands to the list of array indexes.
            status="${SUITE_STATUS[$i]}"  # fetch the status stored at this index.
            if [[ $status == "PASS" ]]; then color="$C_G"; else color="$C_R"; fi  # pick the color that matches PASS or FAIL.
            printf '%-28s %s%s%s\n' "${SUITE_NAMES[$i]}" "$color" "$status" "$C_N"  # print the suite name and its colored result.
        done
    fi
    if [[ $failures -eq 0 ]]; then  # green means everything passed.
        printf '%sfailures: %d%s\n' "$C_G" "$failures" "$C_N"
    else
        printf '%sfailures: %d%s\n' "$C_R" "$failures" "$C_N"  # red means at least one suite failed.
    fi
}

# 1. Shell lint
if [[ $DO_LINT -eq 1 ]]; then  # run the linter only when it was not disabled.
    echo "===== shell lint ====="
    if "$ROOT/scripts/qa/lint_sh.sh"; then record "shell lint" 0; else record "shell lint" 1; fi  # record PASS/FAIL instead of aborting, so later suites still run.
fi

# 2. Build + unit tests
if [[ $DO_BUILD -eq 1 ]]; then  # build only when the flag is on.
    echo "===== build ====="
    if [[ ! -x "$ROOT/build/Release/server" || ! -x "$ROOT/build/Debug/server" ]]; then  # -x checks that both server binaries already exist.
        if "$ROOT/build.sh"; then record "build" 0; else record "build" 1; fi  # run the full build script when either binary is missing.
    else
        echo "build artifacts present; skipping full build (use --no-build to force, or remove build/ to rebuild)"
        record "build" 0  # reuse existing binaries and count this step as a pass.
    fi
fi

if [[ $DO_UNIT -eq 1 ]]; then  # unit tests only when enabled.
    echo "===== unit tests (Debug) ====="
    if [[ -d "$ROOT/build/Debug" ]]; then  # -d checks the Debug build folder exists.
        # Save full output to a temp log; the perl filter shows a compact per-test progress line instead.
        LOG="$(mktemp)"  # mktemp makes a uniquely named log file for full output.
        ctest --test-dir "$ROOT/build/Debug" --progress --output-on-failure 2>&1 | tee "$LOG" | perl -ne 'if (/Start\s+(\d+):\s*(.*)/) { printf "\r[%s] %s", $1, $2; $| = 1; } END { print "\n" }'  # tee saves the log; perl shows a compact one-line-per-test progress view.
        # PIPESTATUS[0] is ctest's status, not tee/perl's; with no -e a failure must be captured explicitly here.
        status=${PIPESTATUS[0]}  # PIPESTATUS[0] is ctest's status; tee/perl statuses come after it.
        if [[ $status -ne 0 ]]; then  # if the tests failed, show the details.
            # The progress view hides failure details, so surface the tail of the saved log.
            echo "---- failing test output (Debug) ----" >&2
            tail -60 "$LOG" >&2  # print the last 60 lines of the saved log.
        fi
        rm -f "$LOG"  # remove the temporary log file.
        if [[ $status -eq 0 ]]; then record "unit tests (Debug)" 0; else record "unit tests (Debug)" 1; fi  # record PASS or FAIL for the Debug run.
    else
        echo "build/Debug missing; skipping" >&2  # a missing build folder means these tests cannot run.
        record "unit tests (Debug)" 1  # count the missing build as a failure.
    fi
    echo "===== unit tests (Release) ====="
    if [[ -d "$ROOT/build/Release" ]]; then
        # Same capture-to-log plus compact-progress approach as the Debug run above.
        LOG="$(mktemp)"  # same save-log-plus-compact-progress approach for Release.
        ctest --test-dir "$ROOT/build/Release" --progress --output-on-failure 2>&1 | tee "$LOG" | perl -ne 'if (/Start\s+(\d+):\s*(.*)/) { printf "\r[%s] %s", $1, $2; $| = 1; } END { print "\n" }'
        # Read ctest's status from the first pipeline stage, not from the formatters.
        status=${PIPESTATUS[0]}  # capture ctest's status from the first pipeline stage again.
        if [[ $status -ne 0 ]]; then
            echo "---- failing test output (Release) ----" >&2
            tail -60 "$LOG" >&2
        fi
        rm -f "$LOG"
        if [[ $status -eq 0 ]]; then record "unit tests (Release)" 0; else record "unit tests (Release)" 1; fi  # record PASS or FAIL for the Release run.
    else
        echo "build/Release missing; skipping" >&2
        record "unit tests (Release)" 1
    fi
fi

# 3. Database init (runs by default before starting the server)
if [[ $SKIP_SERVER -eq 0 && $SKIP_DB_INIT -eq 0 ]]; then
    echo "===== database init ====="
    if "$ROOT/scripts/run/db_init.sh"; then record "database init" 0; else record "database init" 1; fi  # initialize the database only when we will start a server and did not skip DB init.
fi

# 4. Start server
# Remember the spawned PID so both the failure path below and the EXIT trap can stop it.
SRV_PID=""  # an empty PID means no server has been started yet.
if [[ $SKIP_SERVER -eq 0 ]]; then  # start a server unless --skip-server was used.
    echo "===== starting server (run4bench configuration) ====="
    # Run in the background with output in a log so server output stays out of test output; $! captures its PID.
    "$ROOT/scripts/run/run4bench.sh" >/tmp/valhalla_run_all.log 2>&1 &  # run in the background with output in a log; $! is the new process ID.
    SRV_PID=$!  # save that process ID for later cleanup.
    # Wait for health
    # Poll up to ~30s because startup (DB connection, migrations) is not instantaneous.
    for _ in $(seq 1 30); do  # poll up to 30 times, with a one-second pause each time.
        if curl -fsS "$BASE_URL/hello" >/dev/null 2>&1; then  # curl -f fails on HTTP errors, so success means the API is healthy.
            echo "server is up"
            break  # leave the polling loop as soon as the server answers.
        fi
        sleep 1  # wait one second between health checks.
    done
    if ! curl -fsS "$BASE_URL/hello" >/dev/null 2>&1; then  # do one final check after all polling attempts.
        echo "server failed to start; log:" >&2
        tail -40 /tmp/valhalla_run_all.log >&2  # show the end of the server log to help debugging.
        record "server start" 1  # record a failed server start.
        print_summary  # show the results gathered so far before giving up.
        # clean up
        # The EXIT trap is installed only after a successful health check, so handle this path explicitly.
        if [[ -n $SRV_PID ]]; then kill "$SRV_PID" 2>/dev/null || true; wait "$SRV_PID" 2>/dev/null || true; fi  # the EXIT trap is installed only after this point, so clean up by hand on this path.
        exit 1  # exit 1 because the tests cannot run without a server.
    fi
    record "server start" 0  # the health check passed, so record a successful start.
fi

# @brief Stop the background server on exit unless --skip-server or --keep-server was requested.
# @return None; kills and waits for $SRV_PID, ignoring errors if it already exited.
# shellcheck disable=SC2329,SC2317
cleanup() {  # stop the server only if we started it and were not asked to keep it.
    if [[ $SKIP_SERVER -eq 0 && $KEEP_SERVER -eq 0 && -n $SRV_PID ]]; then
        echo "===== stopping server ====="
        kill "$SRV_PID" 2>/dev/null || true  # TERM asks the process to shut down gracefully.
        wait "$SRV_PID" 2>/dev/null || true  # wait for it to actually exit.
    fi
}
# Guarantee server shutdown on every exit path (normal, early error, or signal) since -e is not used.
trap cleanup EXIT  # the trap guarantees cleanup on every exit path, including errors.

# 5. API tests
if [[ $DO_API -eq 1 ]]; then
    echo "===== test_api.sh ====="
    if "$ROOT/scripts/test/test_api.sh"; then record "test_api" 0; else record "test_api" 1; fi  # record whether the API tests passed.
fi

# 6. fuzz_auth
if [[ $DO_FUZZ -eq 1 ]]; then
    echo "===== fuzz_auth.sh ====="
    if "$ROOT/scripts/test/fuzz_auth.sh"; then record "fuzz_auth" 0; else record "fuzz_auth" 1; fi  # record the fuzz-auth result.
fi

# 7. CORS
if [[ $DO_CORS -eq 1 ]]; then
    echo "===== cors_test.sh ====="
    if "$ROOT/scripts/test/cors_test.sh" "http://localhost:$PORT"; then record "cors_test" 0; else record "cors_test" 1; fi  # record CORS for a plain HTTP origin.
    echo "===== cors_react_check.sh ====="
    if "$ROOT/scripts/test/cors_react_check.sh" "http://localhost:$PORT"; then record "cors_react_check" 0; else record "cors_react_check" 1; fi  # record CORS for a React-style origin.
fi

# 8. sqlmap (opt-in)
if [[ $DO_SQLMAP -eq 1 ]]; then
    echo "===== sqlmap_test.sh ====="
    if "$ROOT/scripts/test/sqlmap_test.sh"; then record "sqlmap" 0; else record "sqlmap" 1; fi  # record the optional sqlmap result.
fi

# 9. schemathesis (opt-in)
if [[ $DO_SCHEMATHESIS -eq 1 ]]; then
    echo "===== schemathesis_test.sh ====="
    if "$ROOT/scripts/test/schemathesis_test.sh"; then record "schemathesis" 0; else record "schemathesis" 1; fi  # record the optional schemathesis result.
fi

# 10. perf baseline (opt-in)
if [[ $DO_PERF -eq 1 ]]; then
    echo "===== perf baseline ====="
    if [[ $PERF_FULL -eq 1 ]]; then
        if "$ROOT/scripts/test/perf_baseline.sh"; then record "perf baseline" 0; else record "perf baseline" 1; fi  # run the full baseline when --perf-full was requested.
    else
        if "$ROOT/scripts/test/perf_baseline.sh" --smoke; then record "perf smoke" 0; else record "perf smoke" 1; fi  # otherwise run the quicker smoke baseline.
    fi
fi

print_summary  # print the summary once at the end.
# Propagate the failed-suite count as the exit status so CI fails when any suite failed.
exit "$failures"  # exit with the number of failed suites so CI can detect the failure.
