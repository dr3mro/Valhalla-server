#!/usr/bin/env bash
# Task runner for the Valhalla-server repository.
# Usage: ./run.sh <command> [args...]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT" || exit 1

# Color/emoji by default (unless --no-color or NO_COLOR).
# Force color/emoji unless the user explicitly asks to disable them.
if [[ " $* " == *" --no-color "* ]]; then
    export VALHALLA_NO_COLOR=1
    export VALHALLA_NO_EMOJI=1
else
    unset NO_COLOR VALHALLA_NO_COLOR VALHALLA_NO_EMOJI
    export FORCE_COLOR=1
    export FORCE_EMOJI=1
fi

# shellcheck disable=SC1091
source "$ROOT/scripts/lib/ui.sh"

STATE_DIR="$ROOT/build/run"
PID_FILE="$STATE_DIR/server.pid"
LOG_FILE="$STATE_DIR/server.log"
mkdir -p "$STATE_DIR"

# shellcheck disable=SC2153
usage() {
    cat <<EOF
${C_BOLD}${C_M}╔═══════════════════════════════════════╗${C_N}
${C_BOLD}${C_M}║${C_N}           ${C_BOLD}${C_Y}Valhalla Run Tool${C_N}           ${C_BOLD}${C_M}║${C_N}
${C_BOLD}${C_M}╚═══════════════════════════════════════╝${C_N}

${C_BOLD}Usage:${C_N} ./run.sh <command> [args...]

${C_BOLD}${C_Y}${E_BUILD}Build & run:${C_N}
  ${C_C}conan init${C_N}              ${C_DIM}Detect conan profile${C_N}
  ${C_C}conan install${C_N}           ${C_DIM}Install deps (both)${C_N}
  ${C_C}conan install release${C_N}   ${C_DIM}Install release deps${C_N}
  ${C_C}conan install debug${C_N}     ${C_DIM}Install debug deps${C_N}
  ${C_C}build${C_N}                   ${C_DIM}Build Debug + Release${C_N}
  ${C_C}build release${C_N}           ${C_DIM}Build Release only${C_N}
  ${C_C}build debug${C_N}             ${C_DIM}Build Debug only${C_N}
  ${C_C}build openapi${C_N}           ${C_DIM}Regenerate OpenAPI docs${C_N}
  ${C_C}configure${C_N}               ${C_DIM}Configure both builds (default)${C_N}
  ${C_C}configure release${C_N}       ${C_DIM}Configure Release build${C_N}
  ${C_C}configure debug${C_N}         ${C_DIM}Configure Debug build${C_N}
  ${C_C}clean${C_N}                   ${C_DIM}Clean Debug + Release${C_N}
  ${C_C}clean release${C_N}           ${C_DIM}Clean Release only${C_N}
  ${C_C}clean debug${C_N}             ${C_DIM}Clean Debug only${C_N}
  ${C_C}release${C_N}                 ${C_DIM}Run Release server (./scripts/run/run_release.sh)${C_N}
  ${C_C}debug${C_N}                   ${C_DIM}Run Debug server (./scripts/run/run_debug.sh)${C_N}
${C_BOLD}${C_Y}${E_SERVER}Server lifecycle:${C_N}
  ${C_C}server start release|debug [--foreground]${C_N}    ${C_DIM}Start server (background by default)${C_N}
  ${C_C}server stop${C_N}                                 ${C_DIM}Stop server${C_N}
  ${C_C}server status${C_N}                               ${C_DIM}Server status${C_N}
  ${C_C}server restart release|debug${C_N}                ${C_DIM}Restart server${C_N}
  ${C_C}server peek${C_N}                                 ${C_DIM}Tail server log${C_N}
${C_BOLD}${C_Y}${E_TEST}Tests:${C_N}
  ${C_C}tests | unit${C_N}            ${C_DIM}Run Catch2 unit tests (Debug + Release)${C_N}
  ${C_C}api${C_N}                     ${C_DIM}Run API tests (scripts/test/test_api.sh)${C_N}
  ${C_C}fuzz${C_N}                    ${C_DIM}Run fuzz_auth${C_N}
  ${C_C}sqlmap${C_N}                  ${C_DIM}Run sqlmap tests${C_N}
  ${C_C}schemathesis${C_N}            ${C_DIM}Run schemathesis tests${C_N}
  ${C_C}cors${C_N}                    ${C_DIM}Run CORS tests${C_N}
  ${C_C}bench${C_N}                   ${C_DIM}Run performance baseline${C_N}
  ${C_C}lint${C_N}                    ${C_DIM}Run shell lint${C_N}
  ${C_C}db-init${C_N}                 ${C_DIM}Initialize database${C_N}
  ${C_C}all${C_N}                     ${C_DIM}Build, init DB, start server, run all tests, stop server${C_N}
${C_BOLD}${C_Y}${E_INFO}Other:${C_N}
  ${C_C}list${C_N}                    ${C_DIM}List commands${C_N}
  ${C_C}help${C_N}                    ${C_DIM}Show this help${C_N}
  ${C_C}completion bash|zsh${C_N}     ${C_DIM}Print completion script${C_N}
  ${C_C}completion setup${C_N}        ${C_DIM}Add completion to shell rc${C_N}
${C_BOLD}${C_Y}Examples:${C_N}
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
    local background=1
    shift || true
    for arg in "$@"; do
        [[ $arg == "--foreground" ]] && background=0
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
    local do_unit=1 do_api=1 do_fuzz=1 do_cors=1
    local do_cors_react=0 do_sqlmap=0 do_schemathesis=0
    for arg in "$@"; do
        case "$arg" in
            --debug) profile="debug" ;;
            --release) profile="release" ;;
            --keep-server) keep=1 ;;
            --no-build) NO_BUILD=1 ;;
            --no-unit) do_unit=0 ;;
            --no-api) do_api=0 ;;
            --no-fuzz) do_fuzz=0 ;;
            --no-cors) do_cors=0 ;;
            --cors-react) do_cors_react=1 ;;
            --sqlmap) do_sqlmap=1 ;;
            --schemathesis) do_schemathesis=1 ;;
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

    if server_running; then
        server_stop
    fi
    step "Start server ($profile)"
    server_start "$profile" --background || { err "Server start failed"; return 1; }

    local failures=0
    local -a SUITES=() RESULTS=()
    record() { SUITES+=("$1"); if [[ $2 -eq 0 ]]; then RESULTS+=("${C_G}PASS${C_N}"); else RESULTS+=("${C_R}FAIL${C_N}"); failures=$((failures+1)); fi; }

    if [[ $do_unit -eq 1 ]]; then
        step "Unit tests"
        ctest --test-dir "$ROOT/build/Debug" --progress --output-on-failure 2>&1 | tee /tmp/run_unit.log
        unit_rc=${PIPESTATUS[0]}
        if [[ $unit_rc -eq 0 ]]; then record "unit tests" 0; else record "unit tests" 1; tail -20 /tmp/run_unit.log; fi
    fi

    if [[ $do_api -eq 1 ]]; then
        step "API tests"
        BASE_URL="http://127.0.0.1:8080/api/v2" "$ROOT/scripts/test/test_api.sh" 2>&1 | tee /tmp/run_api.log
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "api tests" 0; else record "api tests" 1; fi
    fi

    if [[ $do_fuzz -eq 1 ]]; then
        step "Fuzz tests"
        BASE_URL="http://127.0.0.1:8080/api/v2" "$ROOT/scripts/test/fuzz_auth.sh" 2>&1 | tee /tmp/run_fuzz.log
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "fuzz tests" 0; else record "fuzz tests" 1; fi
    fi

    if [[ $do_cors -eq 1 ]]; then
        step "CORS tests"
        "$ROOT/scripts/test/cors_test.sh" "http://localhost:8080" 2>&1 | tee /tmp/run_cors.log
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "cors tests" 0; else record "cors tests" 1; fi
    fi

    if [[ $do_cors_react -eq 1 ]]; then
        step "CORS react checks"
        "$ROOT/scripts/test/cors_react_check.sh" "http://localhost:8080" 2>&1 | tee /tmp/run_cors_react.log
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "cors react" 0; else record "cors react" 1; fi
    fi

    if [[ $do_sqlmap -eq 1 ]]; then
        step "SQLMap tests"
        "$ROOT/scripts/test/sqlmap_test.sh" 2>&1 | tee /tmp/run_sqlmap.log
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "sqlmap" 0; else record "sqlmap" 1; fi
    fi

    if [[ $do_schemathesis -eq 1 ]]; then
        step "Schemathesis tests"
        "$ROOT/scripts/test/schemathesis_test.sh" 2>&1 | tee /tmp/run_schemathesis.log
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "schemathesis" 0; else record "schemathesis" 1; fi
    fi

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
    completion)
        shell="${1:-bash}"
        if [[ $shell == "setup" ]]; then
            shell="${2:-}"
            if [[ -z $shell ]]; then
                case "${SHELL:-}" in
                    *zsh*) shell="zsh" ;;
                    *bash*) shell="bash" ;;
                    *) err "Could not detect shell; use: ./run.sh completion setup bash|zsh"; exit 2 ;;
                esac
            fi
            case "$shell" in
                bash)
                    rc="$HOME/.bashrc"
                    [[ -f $HOME/.bash_profile ]] && rc="$HOME/.bash_profile"
                    ;;
                zsh) rc="$HOME/.zshrc" ;;
                *) err "Unknown shell: $shell"; exit 2 ;;
            esac
            marker="# run.sh completion (auto-added)"
            if grep -qF "$marker" "$rc" 2>/dev/null; then
                ok "Completion already configured in $rc"
                exit 0
            fi
            {
                echo ""
                echo "$marker"
                echo "eval \"\$($ROOT/run.sh completion $shell)\""
            } >> "$rc"
            ok "Added completion to $rc (restart your shell or run: source $rc)"
            exit 0
        fi
        case "$shell" in
            bash)
                cat <<'BASH_COMPLETION'
_run_sh() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"
    local commands="build release debug configure clean server tests api fuzz sqlmap schemathesis cors bench lint db-init all conan list help completion"
    local options="--help --no-color --no-build --keep-server --skip-server --skip-db-init --background --port --base-url"
    if [[ $cur == -* ]]; then
        COMPREPLY=( $(compgen -W "$options" -- "$cur") )
        return 0
    fi
    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "$commands" -- "$cur") )
        return 0
    fi
    case "${COMP_WORDS[1]}" in
        build) COMPREPLY=( $(compgen -W "release debug openapi all" -- "$cur") ) ;;
        configure|clean) COMPREPLY=( $(compgen -W "release debug both all" -- "$cur") ) ;;
        tests|unit) COMPREPLY=( $(compgen -W "debug release both all" -- "$cur") ) ;;
        server)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "start stop status restart peek" -- "$cur") )
            elif [[ "${COMP_WORDS[2]}" == "start" || "${COMP_WORDS[2]}" == "restart" ]]; then
                COMPREPLY=( $(compgen -W "release debug" -- "$cur") )
            fi
            ;;
        conan)
            if [[ $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "init install" -- "$cur") )
            elif [[ "${COMP_WORDS[2]}" == "install" ]]; then
                COMPREPLY=( $(compgen -W "release debug both all" -- "$cur") )
            fi
            ;;
    esac
    return 0
}
complete -F _run_sh run.sh
BASH_COMPLETION
                ;;
            zsh)
                cat <<'ZSH_COMPLETION'
#compdef run.sh
_run_sh() {
    local -a commands
    commands=(
        'build:Build Debug+Release'
        'release:Run Release server'
        'debug:Run Debug server'
        'configure:Configure builds'
        'clean:Clean builds'
        'server:Server lifecycle'
        'tests:Run unit tests'
        'api:Run API tests'
        'fuzz:Run fuzz_auth'
        'sqlmap:Run sqlmap tests'
        'schemathesis:Run schemathesis'
        'cors:Run CORS tests'
        'bench:Run perf baseline'
        'lint:Run shell lint'
        'db-init:Init database'
        'all:Build, test, stop'
        'conan:Conan helpers'
        'list:List commands'
        'help:Show help'
        'completion:Print completion script'
    )
    if [[ "$words[CURRENT]" == -* ]]; then
        compadd --help --no-color --no-build --keep-server --skip-server --skip-db-init --background --port --base-url
        return
    fi
    if (( CURRENT == 2 )); then
        _describe 'command' commands
        return
    fi
    case "$words[2]" in
        build) compadd release debug openapi all ;;
        configure|clean) compadd release debug both all ;;
        tests|unit) compadd debug release both all ;;
        server)
            if (( CURRENT == 3 )); then compadd start stop status restart peek
            elif [[ "$words[3]" == (start|restart) ]]; then compadd release debug
            fi ;;
        conan)
            if (( CURRENT == 3 )); then compadd init install
            elif [[ "$words[3]" == install ]]; then compadd release debug both all
            fi ;;
    esac
}
compdef _run_sh run.sh
ZSH_COMPLETION
                ;;
            *) err "Unknown shell: $shell (use bash or zsh)"; exit 2 ;;
        esac
        ;;
    list) list_cmds ;;
    help|-h|--help) usage ;;
    conan)
        sub="${1:-}"; shift || true
        case "$sub" in
            init) conan profile detect --force ;;
            install)
                profile="${1:-both}"; shift || true
                case "$profile" in
                    release) conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" ;;
                    debug) conan install . --build=missing -s build_type=Debug -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" ;;
                    both|all|"") conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" && conan install . --build=missing -s build_type=Debug -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" ;;
                    *) err "Unknown conan install profile: $profile"; exit 2 ;;
                esac
                ;;
            *) err "Unknown conan command: $sub"; exit 2 ;;
        esac
        ;;
    build)
        profile="${1:-all}"; shift || true
        case "$profile" in
            openapi)
                "$ROOT/tools/docs/build.sh" "$@"
                ;;
            release|debug|all|"")
                "$ROOT/tools/docs/build.sh" >/dev/null 2>&1 || true
                case "$profile" in
                    release) cmake --build "$ROOT/build/Release" "$@" ;;
                    debug) cmake --build "$ROOT/build/Debug" "$@" ;;
                    all|"") "$ROOT/scripts/build/build.sh" "$@" ;;
                esac
                ;;
            *) err "Unknown build profile: $profile"; exit 2 ;;
        esac
        ;;
    configure)
        profile="${1:-all}"; shift || true
        case "$profile" in
            release) cmake -S "$ROOT" -B "$ROOT/build/Release" -DCMAKE_BUILD_TYPE=Release -G Ninja "$@" ;;
            debug) cmake -S "$ROOT" -B "$ROOT/build/Debug" -DCMAKE_BUILD_TYPE=Debug -G Ninja "$@" ;;
            both|all|"") cmake -S "$ROOT" -B "$ROOT/build/Release" -DCMAKE_BUILD_TYPE=Release -G Ninja "$@" && cmake -S "$ROOT" -B "$ROOT/build/Debug" -DCMAKE_BUILD_TYPE=Debug -G Ninja "$@" ;;
            *) err "Unknown configure profile: $profile"; exit 2 ;;
        esac
        ;;
    clean)
        profile="${1:-all}"; shift || true
        case "$profile" in
            release) cmake --build "$ROOT/build/Release" --target clean "$@" ;;
            debug) cmake --build "$ROOT/build/Debug" --target clean "$@" ;;
            all|"") cmake --build "$ROOT/build/Release" --target clean "$@"; cmake --build "$ROOT/build/Debug" --target clean "$@" ;;
            *) err "Unknown clean profile: $profile"; exit 2 ;;
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
