#!/usr/bin/env bash
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
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT" || exit 1

# Defaults
DO_BUILD=1
DO_UNIT=1
DO_LINT=1
DO_API=1
DO_FUZZ=1
DO_CORS=1
DO_SQLMAP=0
DO_SCHEMATHESIS=0
DO_PERF=0
PERF_FULL=0
SKIP_DB_INIT=0
SKIP_SERVER=0
KEEP_SERVER=0
PORT=8080
BASE_URL=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) DO_SQLMAP=1; DO_SCHEMATHESIS=1; DO_PERF=1; shift ;;
        --no-build) DO_BUILD=0; shift ;;
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
        --base-url) BASE_URL="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        -h|--help) sed -n '3,32p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

BASE_URL="${BASE_URL:-http://127.0.0.1:$PORT/api/v2}"
export BASE_URL

failures=0
SUITE_NAMES=()
SUITE_STATUS=()

record() { # name status
    if [[ $2 -ne 0 ]]; then
        echo "FAIL: $1" >&2
        failures=$((failures + 1))
        SUITE_STATUS+=("FAIL")
    else
        echo "PASS: $1"
        SUITE_STATUS+=("PASS")
    fi
    SUITE_NAMES+=("$1")
}

print_summary() {
    echo "===== summary ====="
    printf '%-28s %s\n' "Suite" "Result"
    printf '%-28s %s\n' "----------------------------" "------"
    if [[ ${#SUITE_NAMES[@]} -gt 0 ]]; then
        local i
        for i in "${!SUITE_NAMES[@]}"; do
            printf '%-28s %s\n' "${SUITE_NAMES[$i]}" "${SUITE_STATUS[$i]}"
        done
    fi
    echo "failures: $failures"
}

# 1. Shell lint
if [[ $DO_LINT -eq 1 ]]; then
    echo "===== shell lint ====="
    if "$ROOT/tools/lint_sh.sh"; then record "shell lint" 0; else record "shell lint" 1; fi
fi

# 2. Build + unit tests
if [[ $DO_BUILD -eq 1 ]]; then
    echo "===== build ====="
    if [[ ! -x "$ROOT/build/Release/server" || ! -x "$ROOT/build/Debug/server" ]]; then
        if "$ROOT/build.sh"; then record "build" 0; else record "build" 1; fi
    else
        echo "build artifacts present; skipping full build (use --no-build to force, or remove build/ to rebuild)"
        record "build" 0
    fi
fi

if [[ $DO_UNIT -eq 1 ]]; then
    echo "===== unit tests (Debug) ====="
    if [[ -d "$ROOT/build/Debug" ]]; then
        if ctest --test-dir "$ROOT/build/Debug" --output-on-failure; then record "unit tests (Debug)" 0; else record "unit tests (Debug)" 1; fi
    else
        echo "build/Debug missing; skipping" >&2
        record "unit tests (Debug)" 1
    fi
    echo "===== unit tests (Release) ====="
    if [[ -d "$ROOT/build/Release" ]]; then
        if ctest --test-dir "$ROOT/build/Release" --output-on-failure; then record "unit tests (Release)" 0; else record "unit tests (Release)" 1; fi
    else
        echo "build/Release missing; skipping" >&2
        record "unit tests (Release)" 1
    fi
fi

# 3. Database init (runs by default before starting the server)
if [[ $SKIP_SERVER -eq 0 && $SKIP_DB_INIT -eq 0 ]]; then
    echo "===== database init ====="
    if "$ROOT/db_init.sh"; then record "database init" 0; else record "database init" 1; fi
fi

# 4. Start server
SRV_PID=""
if [[ $SKIP_SERVER -eq 0 ]]; then
    echo "===== starting server (run4bench configuration) ====="
    "$ROOT/run4bench.sh" >/tmp/valhalla_run_all.log 2>&1 &
    SRV_PID=$!
    # Wait for health
    for _ in $(seq 1 30); do
        if curl -fsS "$BASE_URL/hello" >/dev/null 2>&1; then
            echo "server is up"
            break
        fi
        sleep 1
    done
    if ! curl -fsS "$BASE_URL/hello" >/dev/null 2>&1; then
        echo "server failed to start; log:" >&2
        tail -40 /tmp/valhalla_run_all.log >&2
        record "server start" 1
        print_summary
        # clean up
        if [[ -n $SRV_PID ]]; then kill "$SRV_PID" 2>/dev/null || true; wait "$SRV_PID" 2>/dev/null || true; fi
        exit 1
    fi
    record "server start" 0
fi

# shellcheck disable=SC2329
cleanup() {
    if [[ $SKIP_SERVER -eq 0 && $KEEP_SERVER -eq 0 && -n $SRV_PID ]]; then
        echo "===== stopping server ====="
        kill "$SRV_PID" 2>/dev/null || true
        wait "$SRV_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# 5. API tests
if [[ $DO_API -eq 1 ]]; then
    echo "===== test_api.sh ====="
    if "$ROOT/test_api.sh"; then record "test_api" 0; else record "test_api" 1; fi
fi

# 6. fuzz_auth
if [[ $DO_FUZZ -eq 1 ]]; then
    echo "===== fuzz_auth.sh ====="
    if "$ROOT/fuzz_auth.sh"; then record "fuzz_auth" 0; else record "fuzz_auth" 1; fi
fi

# 7. CORS
if [[ $DO_CORS -eq 1 ]]; then
    echo "===== cors_test.sh ====="
    if "$ROOT/cors_test.sh" "http://localhost:$PORT"; then record "cors_test" 0; else record "cors_test" 1; fi
    echo "===== cors_react_check.sh ====="
    if "$ROOT/cors_react_check.sh" "http://localhost:$PORT"; then record "cors_react_check" 0; else record "cors_react_check" 1; fi
fi

# 8. sqlmap (opt-in)
if [[ $DO_SQLMAP -eq 1 ]]; then
    echo "===== sqlmap_test.sh ====="
    if "$ROOT/sqlmap_test.sh"; then record "sqlmap" 0; else record "sqlmap" 1; fi
fi

# 9. schemathesis (opt-in)
if [[ $DO_SCHEMATHESIS -eq 1 ]]; then
    echo "===== schemathesis_test.sh ====="
    if "$ROOT/schemathesis_test.sh"; then record "schemathesis" 0; else record "schemathesis" 1; fi
fi

# 10. perf baseline (opt-in)
if [[ $DO_PERF -eq 1 ]]; then
    echo "===== perf baseline ====="
    if [[ $PERF_FULL -eq 1 ]]; then
        if "$ROOT/tools/perf_baseline.sh"; then record "perf baseline" 0; else record "perf baseline" 1; fi
    else
        if "$ROOT/tools/perf_baseline.sh" --smoke; then record "perf smoke" 0; else record "perf smoke" 1; fi
    fi
fi

print_summary
exit "$failures"
