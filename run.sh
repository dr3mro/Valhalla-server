#!/usr/bin/env bash
# Task runner for the Valhalla-server repository.
# Usage: ./run.sh <command> [args...]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT" || exit 1

# Color/emoji by default (unless --no-color or NO_COLOR).
if [[ " $* " != *" --no-color "* && -z "${NO_COLOR:-}" ]]; then
    export FORCE_COLOR=1
    export FORCE_EMOJI=1
fi

# shellcheck disable=SC1091
source "$ROOT/scripts/lib/ui.sh"

STATE_DIR="$ROOT/build/run"
PID_FILE="$STATE_DIR/server.pid"
LOG_FILE="$STATE_DIR/server.log"
mkdir -p "$STATE_DIR"

usage() {
    cat <<EOF
${C_BOLD}Valhalla run tool${C_N}

${C_BOLD}Usage:${C_N} ./run.sh <command> [args...]

${C_BOLD}Build & run:${C_N}
  build            Build Debug + Release
  build release    Build Release only
  build debug      Build Debug only
  release          Run Release server (./scripts/run/run_release.sh)
  debug            Run Debug server (./scripts/run/run_debug.sh)
  openapi          Regenerate OpenAPI docs

${C_BOLD}Server lifecycle:${C_N}
  server start release|debug [--background]
  server stop
  server status
  server restart release|debug
  server peek                 Tail the background server log (Ctrl+C to stop; server keeps running)

${C_BOLD}Tests:${C_N}
  tests | unit     Run Catch2 unit tests (Debug + Release)
  api              Run API tests (scripts/test/test_api.sh)
  fuzz             Run fuzz_auth
  sqlmap           Run sqlmap tests
  schemathesis     Run schemathesis tests
  cors             Run CORS tests
  bench            Run performance baseline
  lint             Run shell lint
  db-init          Initialize database
  all              Build, init DB, start server, run all tests, stop server

${C_BOLD}Other:${C_N}
  list             List commands
  help             Show this help

${C_BOLD}Examples:${C_N}
  ./run.sh release --build
  ./run.sh all --debug
  ./run.sh server start release --background
  ./run.sh server stop
EOF
}

# shellcheck disable=SC2153
list_cmds() {
    printf '%s %s\n' "$E_ROCKET" "release"
    printf '%s %s\n' "$E_BUILD" "build"
    printf '%s %s\n' "$E_TEST" "tests"
    printf '%s %s\n' "$E_LINT" "lint"
    printf '%s %s\n' "$E_SERVER" "server"
    printf '%s %s\n' "$E_STOP" "stop (via 'server stop')"
}

server_pid() {
    [[ -f $PID_FILE ]] && cat "$PID_FILE" 2>/dev/null || true
}

server_running() {
    local pid
    pid="$(server_pid)"
    [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null
}

server_start() {
    local profile="${1:-release}"
    local background=0
    shift || true
    for arg in "$@"; do
        [[ $arg == "--background" ]] && background=1
    done

    if server_running; then
        warn "Server already running (PID $(server_pid))"
        return 0
    fi

    local cmd
    case "$profile" in
        release) cmd="$ROOT/scripts/run/run_release.sh" ;;
        debug)   cmd="$ROOT/scripts/run/run_debug.sh" ;;
        *) err "Unknown profile: $profile"; return 2 ;;
    esac

    step "Starting $profile server"
    if [[ $background -eq 1 ]]; then
        "$cmd" >"$LOG_FILE" 2>&1 &
        local pid=$!
        echo "$pid" >"$PID_FILE"
        # wait for health
        local base="http://127.0.0.1:8080/api/v2"
        for _ in $(seq 1 30); do
            if curl -fsS "$base/hello" >/dev/null 2>&1; then
                ok "Server up (PID $pid)"
                return 0
            fi
            sleep 1
        done
        err "Server failed to start; see $LOG_FILE"
        server_stop
        return 1
    else
        exec "$cmd"
    fi
}

server_stop() {
    local pid
    pid="$(server_pid)"
    if [[ -z $pid ]]; then
        info "No server PID recorded"
        return 0
    fi
    if kill -0 "$pid" 2>/dev/null; then
        step "Stopping server (PID $pid)"
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        ok "Server stopped"
    else
        info "Server not running"
    fi
    rm -f "$PID_FILE"
}

server_status() {
    if server_running; then
        ok "Server running (PID $(server_pid))"
        curl -fsS "http://127.0.0.1:8080/api/v2/hello" 2>/dev/null || true
    else
        warn "Server not running"
    fi
}

run_all() {
    local profile="release"
    local keep=0
    for arg in "$@"; do
        case "$arg" in
            --debug) profile="debug" ;;
            --release) profile="release" ;;
            --keep-server) keep=1 ;;
            --no-build) NO_BUILD=1 ;;
        esac
    done
    export NO_BUILD="${NO_BUILD:-0}"

    step "Build"
    if [[ "${NO_BUILD}" != "1" ]]; then
        "$ROOT/scripts/build/build.sh" || { err "Build failed"; return 1; }
    else
        info "Skipping build"
    fi

    step "Database init"
    "$ROOT/scripts/run/db_init.sh" || warn "db_init failed"

    step "Start server ($profile)"
    server_start "$profile" --background || { err "Server start failed"; return 1; }

    local failures=0
    local -a SUITES=() RESULTS=()
    record() { SUITES+=("$1"); if [[ $2 -eq 0 ]]; then RESULTS+=("${C_G}PASS${C_N}"); else RESULTS+=("${C_R}FAIL${C_N}"); failures=$((failures+1)); fi; }

    step "Unit tests"
    ctest --test-dir "$ROOT/build/Debug" --output-on-failure >/tmp/run_unit.log 2>&1
    unit_rc=$?
    if [[ $unit_rc -eq 0 ]]; then record "unit tests" 0; else record "unit tests" 1; tail -20 /tmp/run_unit.log; fi

    step "API tests"
    if BASE_URL="http://127.0.0.1:8080/api/v2" "$ROOT/scripts/test/test_api.sh" >/tmp/run_api.log 2>&1; then record "api tests" 0; else record "api tests" 1; fi

    step "Fuzz tests"
    if BASE_URL="http://127.0.0.1:8080/api/v2" "$ROOT/scripts/test/fuzz_auth.sh" >/tmp/run_fuzz.log 2>&1; then record "fuzz tests" 0; else record "fuzz tests" 1; fi

    if [[ $keep -eq 0 ]]; then
        server_stop
    fi

    echo
    printf '%s%-20s %s%s\n' "$C_BOLD" "Suite" "Result" "$C_N"
    printf '%-20s %s\n' "--------------------" "------"
    local i
    for i in "${!SUITES[@]}"; do
        printf '%-20s %b\n' "${SUITES[$i]}" "${RESULTS[$i]}"
    done
    echo
    if [[ $failures -eq 0 ]]; then
        ok "All tests passed"
    else
        err "$failures test suite(s) failed"
    fi
    return $failures
}

cmd="${1:-help}"
shift || true

case "$cmd" in
    help|-h|--help) usage ;;
    list) list_cmds ;;
    build)
        profile="${1:-all}"; shift || true
        "$ROOT/tools/docs/build.sh" >/dev/null 2>&1 || true
        case "$profile" in
            release) cmake --build "$ROOT/build/Release" "$@" ;;
            debug) cmake --build "$ROOT/build/Debug" "$@" ;;
            all|"") "$ROOT/scripts/build/build.sh" "$@" ;;
            *) err "Unknown build profile: $profile"; exit 2 ;;
        esac
        ;;
    release) "$ROOT/scripts/run/run_release.sh" "$@" ;;
    debug) "$ROOT/scripts/run/run_debug.sh" "$@" ;;
    tests|unit)
        profile="${1:-both}"; shift || true
        case "$profile" in
            debug) ctest --test-dir "$ROOT/build/Debug" --output-on-failure "$@" ;;
            release) ctest --test-dir "$ROOT/build/Release" --output-on-failure "$@" ;;
            both|all|"") ctest --test-dir "$ROOT/build/Debug" --output-on-failure "$@" && ctest --test-dir "$ROOT/build/Release" --output-on-failure "$@" ;;
            *) err "Unknown test profile: $profile"; exit 2 ;;
        esac
        ;;
    api) BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}" "$ROOT/scripts/test/test_api.sh" "$@" ;;
    fuzz) BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}" "$ROOT/scripts/test/fuzz_auth.sh" "$@" ;;
    sqlmap) "$ROOT/scripts/test/sqlmap_test.sh" "$@" ;;
    schemathesis) "$ROOT/scripts/test/schemathesis_test.sh" "$@" ;;
    cors) "$ROOT/scripts/test/cors_test.sh" "$@" ;;
    bench|perf) "$ROOT/scripts/test/perf_baseline.sh" "$@" ;;
    lint) "$ROOT/scripts/qa/lint_sh.sh" "$@" ;;
    db-init) "$ROOT/scripts/run/db_init.sh" "$@" ;;
    openapi) "$ROOT/tools/docs/build.sh" "$@" ;;
    peek) tail -f "$LOG_FILE" ;;
    server)
        sub="${1:-status}"; shift || true
        case "$sub" in
            start) server_start "$@" ;;
            stop) server_stop ;;
            status) server_status ;;
            peek) tail -f "$LOG_FILE" ;;
            restart) profile="${1:-release}"; shift || true; server_stop; server_start "$profile" "$@" ;;
            *) err "Unknown server command: $sub"; usage; exit 2 ;;
        esac
        ;;
    all|test-all) run_all "$@" ;;
    *) err "Unknown command: $cmd"; usage; exit 2 ;;
esac
