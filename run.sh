#!/usr/bin/env bash
# /**
#  * @file run.sh
#  * @brief Repository task runner: build/configure/clean Conan and CMake targets, manage the server lifecycle, run test suites, and emit shell completion and help from one command entry point.
#  * @details Pick a command as the first argument (for example build, server start, api or all) and the script runs the matching helper program; 'all' builds, starts a server, runs the suites and stops it again.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the single front door developers and CI use for everyday tasks, so nobody has to remember long Conan, CMake, CTest and script paths.
#  * It owns the background server's PID and log files under build/run, which is how start, stop, status and peek all find the same server.
#  * Without it, builds, tests and the server lifecycle would be a collection of easy-to-mistype manual commands.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash case statements: https://www.gnu.org/software/bash/manual/html_node/Conditional-Constructs.html
#  * - Bash trap / signals: https://www.gnu.org/software/bash/manual/html_node/Bourne-Shell-Builtins.html
#  * - Conan: https://docs.conan.io/2/
#  * - CMake: https://cmake.org/cmake/help/latest/
#  * - CTest: https://cmake.org/cmake/help/latest/manual/ctest.1.html
#  * - Programmable completion: https://www.gnu.org/software/bash/manual/html_node/A-Programmable-Completion-Example.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Task runner for the Valhalla-server repository.
# Usage: ./run.sh <command> [args...]
# -u catches variable typos and pipefail propagates failures through run_all's
# tee pipelines; -e is omitted because many steps use `|| true` and check status.
set -uo pipefail  # u = unset variable is an error, pipefail = catch pipeline failures; no -e because steps use || true.

# Resolve the repo root from this script's location so paths work from any cwd.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"  # BASH_SOURCE[0] is this script; its folder is the repository root.
cd "$ROOT" || exit 1  # cd into the root so all later paths work.

# Color/emoji by default (unless --no-color or NO_COLOR).
# Force color/emoji unless the user explicitly asks to disable them.
# Match the flag anywhere in the arguments so it works after the command too.
if [[ " $* " == *" --no-color "* ]]; then  # padding the arguments with spaces lets the pattern match --no-color anywhere.
    export VALHALLA_NO_COLOR=1  # export so sourced helpers and child scripts see the setting.
    export VALHALLA_NO_EMOJI=1  # disable emojis as well when colors are turned off.
else
    unset NO_COLOR VALHALLA_NO_COLOR VALHALLA_NO_EMOJI  # clear any inherited color settings when color is wanted.
    export FORCE_COLOR=1  # FORCE_COLOR tells ui.sh to colorize even when output is piped.
    export FORCE_EMOJI=1  # FORCE_EMOJI does the same for emojis.
fi

# Shared helpers provide the color vars and step/ok/warn/err used throughout.
# shellcheck disable=SC1091
source "$ROOT/scripts/lib/ui.sh"  # source loads ui.sh's color variables and message helpers here.

# Runtime state (PID, log, summaries) lives under build/run, not in the source root.
STATE_DIR="$ROOT/build/run"  # keep runtime state under build/run, not in the source tree.
PID_FILE="$STATE_DIR/server.pid"  # file storing the background server's process ID.
LOG_FILE="$STATE_DIR/server.log"  # file storing the server's output.
mkdir -p "$STATE_DIR"  # create the state directory if it does not exist.

# @brief Print the command reference banner.
# @return Nothing.
# shellcheck disable=SC2153
usage() {  # define the big help banner.
    # The heredoc prints the command reference until the EOF marker.
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
  ${C_C}cors-react${C_N}              ${C_DIM}Run React browser CORS checks${C_N}
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

# @brief Print a short list of the main commands.
# @return Nothing.
# shellcheck disable=SC2153
list_cmds() {
    printf '%s %s\n' "$E_ROCKET" "release"  # define the short command list for 'run.sh list'.
    printf '%s %s\n' "$E_BUILD" "build"
    printf '%s %s\n' "$E_TEST" "tests"
    printf '%s %s\n' "$E_TEST" "cors-react"
    printf '%s %s\n' "$E_LINT" "lint"
    printf '%s %s\n' "$E_SERVER" "server"
    printf '%s %s\n' "$E_STOP" "stop (via 'server stop')"
}

# @brief Read the recorded background-server PID.
# @return PID text on stdout, or nothing.
server_pid() {  # read the saved background-server PID.
    if [[ -f $PID_FILE ]]; then  # only try to read when the PID file exists.
        cat "$PID_FILE" 2>/dev/null || true  # cat the file; || true ignores any read error.
    fi
}

# @brief Check whether the PID in PID_FILE is still a live process.
# @return 0 if running, 1 otherwise.
server_running() {  # check whether the recorded server process is still alive.
    local pid  # local keeps the variable inside this function.
    pid="$(server_pid)"  # get the PID using the helper above.
    [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null  # kill -0 only tests that a process exists; it sends no signal.
}

# @brief Start the server in the background, or exec it in the foreground.
# @param profile release (default) or debug.
# @param Remaining args: --foreground / --background.
# @return 0 on success, 1 if the health check fails, 2 for an unknown profile.
server_start() {  # start the server in the background, or replace this script with it in the foreground.
    local profile="${1:-release}"  # ${1:-release} means 'first argument, or release if none'.
    local background=1  # 1 = run in the background, 0 = run in the foreground.
    shift || true  # shift past the profile so the rest are flag arguments.
    for arg in "$@"; do  # loop over the remaining arguments.
        [[ $arg == "--foreground" ]] && background=0  # --foreground switches to foreground mode.
        [[ $arg == "--background" ]] && background=1  # --background switches back to background mode.
    done

    if server_running; then  # refuse to start a second server.
        warn "Server already running (PID $(server_pid))"  # warn but do not fail; the running server is left alone.
        return 0  # return success because a server is running as requested.
    fi

    local cmd  # cmd will hold the launcher script to invoke.
    case "$profile" in  # case maps the profile name to the right launcher.
        release) cmd="$ROOT/scripts/run/run_release.sh" ;;  # release uses the optimized server.
        debug)   cmd="$ROOT/scripts/run/run_debug.sh" ;;  # debug uses the unoptimized server.
        *) err "Unknown profile: $profile"; return 2 ;;  # an unknown profile is an error (status 2).
    esac

    step "Starting $profile server"  # print a step heading.
    if [[ $background -eq 1 ]]; then  # background mode: launch first, then wait for health.
        "$cmd" >"$LOG_FILE" 2>&1 &  # send all server output to the log file.
        local pid=$!  # $! is the process ID of the just-launched server.
        echo "$pid" >"$PID_FILE"  # save the PID for later stop/status commands.
        # wait for health
        local base="http://127.0.0.1:8080/api/v2"  # the health-check URL.
        for _ in $(seq 1 30); do  # poll up to 30 times, once per second.
            if curl -fsS "$base/hello" >/dev/null 2>&1; then  # curl -f treats HTTP error codes as failures.
                ok "Server up (PID $pid)"  # report that the server is up.
                return 0  # return immediately on success.
            fi
            sleep 1  # wait one second between checks.
        done
        err "Server failed to start; see $LOG_FILE"  # report failure with a pointer to the log.
        server_stop  # stop the server that failed to come up.
        return 1  # return failure (status 1).
    else
        exec "$cmd"  # foreground mode replaces this script with the server process.
    fi
}

# @brief Stop the recorded server process and remove the PID file.
# @return 0 always.
server_stop() {  # stop the recorded server process.
    local pid  # read the PID from its file.
    pid="$(server_pid)"
    if [[ -z $pid ]]; then  # no PID recorded means there is nothing to stop.
        info "No server PID recorded"  # tell the user.
        return 0  # return success.
    fi
    if kill -0 "$pid" 2>/dev/null; then  # kill -0 checks that the process exists.
        step "Stopping server (PID $pid)"  # announce the stop.
        kill "$pid" 2>/dev/null || true  # TERM asks for a graceful shutdown.
        wait "$pid" 2>/dev/null || true  # wait for the process to actually exit.
        ok "Server stopped"  # report success.
    else
        info "Server not running"  # tell the user it was not running.
    fi
    rm -f "$PID_FILE"  # remove the PID file.
}

# @brief Report server status and probe its /hello endpoint.
# @return Nothing.
server_status() {  # report server status and probe the /hello endpoint.
    if server_running; then  # check the recorded server is alive.
        ok "Server running (PID $(server_pid))"  # report its PID.
        curl -fsS "http://127.0.0.1:8080/api/v2/hello" 2>/dev/null || true  # show the /hello reply if the server answers.
    else
        warn "Server not running"  # otherwise warn that it is not running.
    fi
}

# @brief Build, init the DB, start the server, run the enabled suites and print a summary.
# @param Options: --debug/--release, --keep-server, --no-build and --no-* toggles.
# @return The number of failed suites (0 on full success).
run_all() {  # build, initialize the DB, start the server, run suites and print a summary.
    local profile="release"  # the 'all' command defaults to the release profile.
    local keep=0  # 0 = stop the server at the end; 1 = keep it running.
    local do_unit=1 do_api=1 do_fuzz=1 do_cors=1  # suite toggles, all enabled by default.
    local do_cors_react=1 do_sqlmap=1 do_schemathesis=1  # optional suites are on too; missing tools are skipped, not fatal.
    for arg in "$@"; do  # loop over the options passed to 'all'.
        case "$arg" in  # case dispatches each option.
            --debug) profile="debug" ;;  # --debug selects the Debug profile.
            --release) profile="release" ;;  # --release selects the Release profile.
            --keep-server) keep=1 ;;  # --keep-server leaves the server running afterwards.
            --no-build) NO_BUILD=1 ;;  # disable the build step.
            --no-unit) do_unit=0 ;;  # disable the unit tests.
            --no-api) do_api=0 ;;  # disable the API tests.
            --no-fuzz) do_fuzz=0 ;;  # disable the fuzz tests.
            --no-cors) do_cors=0 ;;  # disable the CORS tests.
            --cors-react) do_cors_react=1 ;;  # enable the React CORS checks.
            --sqlmap) do_sqlmap=1 ;;  # enable sqlmap.
            --schemathesis) do_schemathesis=1 ;;  # enable schemathesis.
        esac
    done
    # Default NO_BUILD to 0 so the build runs unless --no-build was passed.
    export NO_BUILD="${NO_BUILD:-0}"  # default NO_BUILD to 0 when --no-build was not passed.

    step "Build"  # announce the build step.
    if [[ "${NO_BUILD}" != "1" ]]; then  # run the build unless it was disabled.
        # Stop the whole run if the build fails; later tests would be meaningless.
        "$ROOT/scripts/build/build.sh" || { err "Build failed"; return 1; }  # stop the whole run if the build fails; later tests would be meaningless.
    else
        info "Skipping build"  # tell the user the build was skipped.
    fi

    step "Database init"  # announce database initialization.
    # DB init failure is only a warning: the server may reuse an existing DB.
    "$ROOT/scripts/run/db_init.sh" || warn "db_init failed"  # a DB-init failure is only a warning; an existing database may still work.

    if server_running; then  # stop an old server before starting a new one.
        server_stop  # stop it.
    fi
    step "Start server ($profile)"  # announce the server start.
    server_start "$profile" --background || { err "Server start failed"; return 1; }  # stop the run if the server cannot start.

    local failures=0  # number of failed suites.
    local -a SUITES=() RESULTS=() PLAIN=()  # parallel arrays: names, colored results and plain results.
    # @brief Record a suite result and count a non-zero status as a failure.
    # @param $1 Suite name; $2 Exit status (0 = pass).
    # @return Nothing.
    record() { SUITES+=("$1"); if [[ $2 -eq 0 ]]; then RESULTS+=("${C_G}PASS${C_N}"); PLAIN+=("PASS"); else RESULTS+=("${C_R}FAIL${C_N}"); PLAIN+=("FAIL"); failures=$((failures+1)); fi; }  # record a suite result and count a non-zero status as a failure.
    # @brief Record a suite as SKIP without counting it as a failure.
    # @param $1 Suite name.
    # @return Nothing.
    skip() { SUITES+=("$1"); RESULTS+=("${C_Y}SKIP${C_N}"); PLAIN+=("SKIP"); }  # record a skipped suite without counting it as a failure.

    if [[ $do_unit -eq 1 ]]; then  # run unit tests when enabled.
        step "Unit tests"
        # PIPESTATUS[0] captures the test status because tee would otherwise mask it.
        ctest --test-dir "$ROOT/build/Debug" --progress --output-on-failure 2>&1 | tee /tmp/run_unit.log  # ctest runs the tests; tee also saves full output to a log.
        unit_rc=${PIPESTATUS[0]}  # capture ctest's status before it is overwritten.
        if [[ $unit_rc -eq 0 ]]; then record "unit tests" 0; else record "unit tests" 1; tail -20 /tmp/run_unit.log; fi  # record the result and show the last log lines on failure.
    fi

    if [[ $do_api -eq 1 ]]; then  # run the API tests.
        step "API tests"
        BASE_URL="http://127.0.0.1:8080/api/v2" "$ROOT/scripts/test/test_api.sh" 2>&1 | tee /tmp/run_api.log  # pass a base URL through the environment to the test script.
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "api tests" 0; else record "api tests" 1; fi  # capture the test script's status through the tee pipe.
    fi

    if [[ $do_fuzz -eq 1 ]]; then  # run the fuzz tests.
        step "Fuzz tests"
        BASE_URL="http://127.0.0.1:8080/api/v2" "$ROOT/scripts/test/fuzz_auth.sh" 2>&1 | tee /tmp/run_fuzz.log  # same URL override for the fuzz script.
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "fuzz tests" 0; else record "fuzz tests" 1; fi  # record the fuzz result.
    fi

    if [[ $do_cors -eq 1 ]]; then  # run the CORS tests.
        step "CORS tests"
        "$ROOT/scripts/test/cors_test.sh" "http://localhost:8080" 2>&1 | tee /tmp/run_cors.log  # the CORS check uses localhost rather than 127.0.0.1.
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "cors tests" 0; else record "cors tests" 1; fi  # record the CORS result.
    fi

    if [[ $do_cors_react -eq 1 ]]; then  # run the React-style CORS checks.
        step "CORS react checks"
        "$ROOT/scripts/test/cors_react_check.sh" "http://localhost:8080" 2>&1 | tee /tmp/run_cors_react.log  # run the browser-based check script.
        if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "cors react" 0; else record "cors react" 1; fi  # record the React CORS result.
    fi

    if [[ $do_sqlmap -eq 1 ]]; then  # sqlmap is optional.
        # Skip optional tools instead of failing the run when they are absent.
        if ! command -v sqlmap >/dev/null 2>&1; then  # skip it when the tool is not installed.
            skip "sqlmap"  # mark the suite as SKIP.
        else
            step "SQLMap tests"
            "$ROOT/scripts/test/sqlmap_test.sh" 2>&1 | tee /tmp/run_sqlmap.log  # run sqlmap when it is available.
            if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "sqlmap" 0; else record "sqlmap" 1; fi  # record the sqlmap result.
        fi
    fi

    if [[ $do_schemathesis -eq 1 ]]; then  # schemathesis is optional too.
        # Either the st CLI or the Python module is enough to run schemathesis.
        if ! command -v st >/dev/null 2>&1 && ! python3 -c 'import schemathesis' 2>/dev/null; then
            skip "schemathesis"  # mark the suite as SKIP.
        else
            step "Schemathesis tests"
            "$ROOT/scripts/test/schemathesis_test.sh" 2>&1 | tee /tmp/run_schemathesis.log  # run schemathesis when it is available.
            if [[ ${PIPESTATUS[0]} -eq 0 ]]; then record "schemathesis" 0; else record "schemathesis" 1; fi  # record the schemathesis result.
        fi
    fi

    if [[ $keep -eq 0 ]]; then  # stop the server unless --keep-server was passed.
        server_stop  # stop it.
    fi

    echo  # print a blank line before the table.
    printf '%s%-20s %s%s\n' "$C_BOLD" "Suite" "Result" "$C_N"  # print the table header.
    printf '%-20s %s\n' "--------------------" "------"  # print the underline.
    local i  # loop index variable.
    for i in "${!SUITES[@]}"; do  # iterate over the suite indexes.
        printf '%-20s %b\n' "${SUITES[$i]}" "${RESULTS[$i]}"  # %b interprets the stored color escapes.
    done
    echo  # print a blank line after the table.
    # Write a summary report file.
    # Best-effort: never fail the run just because the summary file cannot be written.
    {  # write a plain-text summary file.
        echo "Valhalla test summary - $(date)"  # include the date in the first line.
        echo "failures: $failures"  # write the failure count.
        for i in "${!SUITES[@]}"; do  # one line per suite.
            printf '%-20s %s\n' "${SUITES[$i]}" "${PLAIN[$i]}"  # write the uncolored result.
        done
    } > "$STATE_DIR/test-summary.txt" 2>/dev/null || true  # best effort: never fail just because the summary file could not be written.

    if [[ $failures -eq 0 ]]; then  # all suites passed.
        ok "All tests passed"
    else  # report how many suites failed.
        err "$failures test suite(s) failed"
    fi
    info "Summary written to $STATE_DIR/test-summary.txt"  # tell the user where the summary is.
    # Use the failed-suite count as the exit status for CI.
    return $failures  # use the failure count as the exit status for CI.
}

# Default to the help screen when no command is given.
cmd="${1:-help}"  # show help when no command is given.
shift || true  # shift past the command, or do nothing if there were no arguments.

case "$cmd" in  # dispatch on the command name.
    completion)  # the completion subcommand.
        shell="${1:-bash}"  # default completion shell is bash.
        if [[ $shell == "setup" ]]; then  # 'setup' edits the shell rc file instead of printing a script.
            shell="${2:-}"  # the shell name is the second argument.
            if [[ -z $shell ]]; then  # if no shell was named, detect it from $SHELL.
                case "${SHELL:-}" in  # case on the login shell.
                    *zsh*) shell="zsh" ;;  # zsh detected.
                    *bash*) shell="bash" ;;  # bash detected.
                    *) err "Could not detect shell; use: ./run.sh completion setup bash|zsh"; exit 2 ;;  # unknown shell is an error.
                esac
            fi
            case "$shell" in  # choose the rc file for the selected shell.
                bash)  # bash branch.
                    # macOS Terminal login shells read .bash_profile instead of .bashrc.
                    rc="$HOME/.bashrc"  # default bash rc file.
                    [[ -f $HOME/.bash_profile ]] && rc="$HOME/.bash_profile"  # macOS login shells read .bash_profile instead.
                    ;;
                zsh) rc="$HOME/.zshrc" ;;  # zsh rc file.
                *) err "Unknown shell: $shell"; exit 2 ;;  # unknown shell.
            esac
            # A fixed marker makes the rc edit idempotent across repeated setup runs.
            marker="# run.sh completion (auto-added)"  # a fixed marker makes repeated setup safe (idempotent).
            if grep -qF "$marker" "$rc" 2>/dev/null; then  # if the marker is already present, do nothing.
                ok "Completion already configured in $rc"  # tell the user completion is already configured.
                exit 0  # exit successfully.
            fi
            {  # append the marker and the eval line to the rc file.
                echo ""  # start with a blank line.
                echo "$marker"  # write the marker comment.
                echo "eval \"\$($ROOT/run.sh completion $shell)\""  # write the eval line that loads completion on startup.
            } >> "$rc"
            ok "Added completion to $rc (restart your shell or run: source $rc)"  # tell the user what was added.
            exit 0  # exit successfully.
        fi
        case "$shell" in  # print a completion script for the chosen shell.
            bash)  # bash completion branch.
                # A quoted heredoc prints the completion code literally, with no variable expansion.
                cat <<'BASH_COMPLETION'
_run_sh() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"
    local commands="build release debug configure clean server tests api fuzz sqlmap schemathesis cors cors-react bench lint db-init all conan list help completion"
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
            zsh)  # zsh completion branch.
                # Same idea for zsh: print the completion definition as-is.
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
        'cors-react:Run React browser CORS checks'
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
            *) err "Unknown shell: $shell (use bash or zsh)"; exit 2 ;;  # unknown completion shell.
        esac
        ;;
    list) list_cmds ;;  # list the main commands.
    help|-h|--help) usage ;;  # show the help banner.
    conan)  # Conan helper commands.
        sub="${1:-}"; shift || true  # pick the Conan subcommand.
        case "$sub" in  # dispatch on it.
            init) conan profile detect --force ;;  # regenerate the Conan profile.
            install)  # install dependencies.
                profile="${1:-both}"; shift || true  # default Conan install profile is 'both'.
                case "$profile" in  # dispatch on the profile.
                    # The cxxflags conf suppresses unused-result warnings from the trantor dependency.
                    release) conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" ;;  # install Release dependencies; the cxxflags conf quiets trantor warnings.
                    debug) conan install . --build=missing -s build_type=Debug -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" ;;  # same install for the Debug build type.
                    both|all|"") conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" && conan install . --build=missing -s build_type=Debug -s compiler.cppstd=gnu20 -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" ;;  # both: run the two installs in sequence.
                    *) err "Unknown conan install profile: $profile"; exit 2 ;;  # unknown Conan install profile.
                esac
                ;;
            *) err "Unknown conan command: $sub"; exit 2 ;;  # unknown Conan command.
        esac
        ;;
    build)  # build subcommand.
        profile="${1:-all}"; shift || true  # default build profile is 'all'.
        case "$profile" in  # dispatch on the profile.
            # Regenerate the OpenAPI document as its own build target.
            openapi)  # regenerate the OpenAPI document as its own target.
                "$ROOT/tools/docs/build.sh" "$@"  # call the docs build script with any extra arguments.
                ;;
            release|debug|all|"")
                "$ROOT/tools/docs/build.sh" >/dev/null 2>&1 || true  # best-effort docs regeneration before compiling.
                case "$profile" in  # dispatch on the profile.
                    release) cmake --build "$ROOT/build/Release" "$@" ;;  # build Release only.
                    debug) cmake --build "$ROOT/build/Debug" "$@" ;;  # build Debug only.
                    all|"") "$ROOT/scripts/build/build.sh" "$@" ;;  # build both through the full build script.
                esac
                ;;
            *) err "Unknown build profile: $profile"; exit 2 ;;  # unknown build profile.
        esac
        ;;
    configure)  # configure subcommand.
        profile="${1:-all}"; shift || true  # default configure profile is 'all'.
        case "$profile" in  # dispatch on the profile.
            release) cmake -S "$ROOT" -B "$ROOT/build/Release" -DCMAKE_BUILD_TYPE=Release -G Ninja "$@" ;;  # configure only the Release build tree.
            debug) cmake -S "$ROOT" -B "$ROOT/build/Debug" -DCMAKE_BUILD_TYPE=Debug -G Ninja "$@" ;;  # configure only the Debug build tree.
            both|all|"") cmake -S "$ROOT" -B "$ROOT/build/Release" -DCMAKE_BUILD_TYPE=Release -G Ninja "$@" && cmake -S "$ROOT" -B "$ROOT/build/Debug" -DCMAKE_BUILD_TYPE=Debug -G Ninja "$@" ;;  # configure both trees in sequence.
            *) err "Unknown configure profile: $profile"; exit 2 ;;  # unknown configure profile.
        esac
        ;;
    # Clean removes build artifacts only; sources are left untouched.
    clean)  # clean subcommand; this removes build artifacts only, never sources.
        profile="${1:-all}"; shift || true  # default clean profile is 'all'.
        case "$profile" in  # dispatch on the profile.
            release) cmake --build "$ROOT/build/Release" --target clean "$@" ;;  # clean the Release build tree.
            debug) cmake --build "$ROOT/build/Debug" --target clean "$@" ;;  # clean the Debug build tree.
            all|"") cmake --build "$ROOT/build/Release" --target clean "$@"; cmake --build "$ROOT/build/Debug" --target clean "$@" ;;  # clean both trees.
            *) err "Unknown clean profile: $profile"; exit 2 ;;  # unknown clean profile.
        esac
        ;;
    release) "$ROOT/scripts/run/run_release.sh" "$@" ;;  # run the Release server script directly.
    debug) "$ROOT/scripts/run/run_debug.sh" "$@" ;;  # run the Debug server script directly.
    tests|unit)  # unit-test subcommand.
        profile="${1:-both}"; shift || true  # default test profile is 'both'.
        case "$profile" in  # dispatch on the profile.
            debug) ctest --test-dir "$ROOT/build/Debug" --output-on-failure "$@" ;;  # run CTest on the Debug tree.
            release) ctest --test-dir "$ROOT/build/Release" --output-on-failure "$@" ;;  # run CTest on the Release tree.
            both|all|"") ctest --test-dir "$ROOT/build/Debug" --output-on-failure "$@" && ctest --test-dir "$ROOT/build/Release" --output-on-failure "$@" ;;  # run both test trees in sequence.
            *) err "Unknown test profile: $profile"; exit 2 ;;  # unknown test profile.
        esac
        ;;
    # Let callers override the API base URL; tests assume a live server by default.
    api) BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}" "$ROOT/scripts/test/test_api.sh" "$@" ;;  # ${BASE_URL:-...} lets callers override the URL; tests assume a live server by default.
    fuzz) BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}" "$ROOT/scripts/test/fuzz_auth.sh" "$@" ;;  # fuzz tests with the same URL default.
    sqlmap) "$ROOT/scripts/test/sqlmap_test.sh" "$@" ;;  # run the sqlmap test script.
    schemathesis) "$ROOT/scripts/test/schemathesis_test.sh" "$@" ;;  # run the schemathesis test script.
    cors) "$ROOT/scripts/test/cors_test.sh" "$@" ;;  # run the CORS test script.
    cors-react|cors_react|react-cors) "$ROOT/scripts/test/cors_react_check.sh" "$@" ;;  # run the browser-based CORS script (several aliases work).
    bench|perf) "$ROOT/scripts/test/perf_baseline.sh" "$@" ;;  # run the performance baseline.
    lint) "$ROOT/scripts/qa/lint_sh.sh" "$@" ;;  # run the shell linter.
    db-init) "$ROOT/scripts/run/db_init.sh" "$@" ;;  # initialize the database.

    # Follow the server log written by server_start.
    peek) tail -f "$LOG_FILE" ;;  # follow the server log written by server_start.
    server)  # server lifecycle subcommand.
        sub="${1:-status}"; shift || true  # default subcommand is 'status'.
        case "$sub" in  # dispatch on the subcommand.
            start) server_start "$@" ;;  # start the server.
            stop) server_stop ;;  # stop the server.
            status) server_status ;;  # report server status.
            # Follow the server log written by server_start.
            peek) tail -f "$LOG_FILE" ;;  # follow the server log.
            # Restart stops the old process first, then starts the requested profile.
            restart) profile="${1:-release}"; shift || true; server_stop; server_start "$profile" "$@" ;;  # restart means stop the old process, then start the requested profile.
            *) err "Unknown server command: $sub"; usage; exit 2 ;;  # unknown server subcommand.
        esac
        ;;
    all|test-all) run_all "$@" ;;  # 'all' and 'test-all' run the full pipeline.
    *) err "Unknown command: $cmd"; usage; exit 2 ;;  # unknown top-level command.
esac
