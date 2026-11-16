#!/usr/bin/env bash
# /**
#  * @file test_scripts.sh
#  * @brief Smoke/regression tests for the repository's shell scripts: syntax, executable bits, run.sh dispatch, layout, and QA tool availability.
#  * @details Run quick smoke tests over the repository's shell scripts: syntax, executable bits, run.sh commands, file layout and QA tools.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * It gives fast feedback before a full build, so broken scripts are caught early.
#  * CI can run it to confirm the command dispatcher and helper scripts still work.
#  * Without it, a typo in run.sh or a lost executable bit would only be found much later.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - bash -n (syntax check): https://www.gnu.org/software/bash/manual/bash.html
#  * - test builtin: https://www.gnu.org/software/bash/manual/bash.html
#  * - find: https://www.gnu.org/software/findutils/manual/html_node/find_html/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# shellcheck disable=SC2015
# Smoke/regression tests for the repository's shell scripts.
# Checks syntax, help output, and basic dispatch behaviour.
# -u and pipefail catch typos and hidden pipeline errors; -e is omitted because failures are counted, not fatal.
set -uo pipefail  # u and pipefail catch mistakes; no -e because failures are counted, not fatal.

# Resolve the repository root from this script's location so checks run from a consistent directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # BASH_SOURCE[0] is this script's path; ../.. climbs to the repository root.
cd "$ROOT" || exit 1  # cd into the root so relative paths used by the tests resolve correctly.

# Counters; FAILED keeps the names of failing checks for the final report.
PASS=0; FAIL=0; FAILED=()  # PASS/FAIL count results; FAILED remembers names for the final report.
# @brief Record a passing check and print its name.
# @param $1 check name.
# @return None; increments the global PASS counter.
ok()   { PASS=$((PASS+1)); printf '  ok    %s\n' "$1"; }  # Add one to PASS and print an 'ok' line.
# @brief Record a failing check, remember its name, and print it.
# @param $1 check name.
# @return None; increments the global FAIL counter and appends to FAILED.
bad()  { FAIL=$((FAIL+1)); FAILED+=("$1"); printf '  FAIL  %s\n' "$1"; }  # Add one to FAIL, remember the name and print a 'FAIL' line.
# @brief Run a command expected to succeed and record the result.
# @param $1 check name; remaining arguments are the command and its arguments.
# @return None; output is suppressed and only PASS/FAIL state is updated.
check() { # name cmd...
    local name="$1"; shift  # shift removes the check name, leaving the command and its arguments.
    if "$@" >/dev/null 2>&1; then ok "$name"; else bad "$name"; fi  # Redirect output to /dev/null so only the result line is shown.
}
# @brief Run a command expected to fail; succeeding unexpectedly is itself a failure.
# @param $1 check name; remaining arguments are the command and its arguments.
# @return None; output is suppressed and only PASS/FAIL state is updated.
fails() { # name cmd...
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then bad "$name (expected failure)"; else ok "$name"; fi  # For fails(), a command that succeeds is itself the failure.
}

# @brief Print a section heading to group related checks.
# @param $1 section title.
# @return printf's exit status.
section() { printf '\n== %s ==\n' "$1"; }  # Print a labeled heading to group related checks.

# 1. Syntax checks
section "syntax"  # Begin the syntax-check section.
# Pick bash -n or sh -n from each script's shebang.
while IFS= read -r f; do  # Loop over every .sh file under scripts/.
    case "$(head -n1 "$f")" in  # Pick the syntax checker from the script's shebang.
        *bash*) bash -n "$f" && ok "bash -n $f" || bad "bash -n $f" ;;  # bash -n only parses the file; it does not execute any commands.
        *) sh -n "$f" && ok "sh -n $f" || bad "sh -n $f" ;;  # sh -n parses scripts that declare a POSIX shell.
    esac
# Process substitution feeds the loop from the current shell, so PASS/FAIL updates persist.
done < <(find scripts -name '*.sh' -type f)  # find supplies the files; <(...) keeps the counters in this shell.

# 2. Executable bits
section "executable"
# These entry points are invoked directly by CI and other scripts, so the executable bit is required.
for f in run.sh scripts/build/*.sh scripts/run/*.sh scripts/test/*.sh scripts/qa/*.sh; do  # These entry points are invoked directly, so they must be executable.
    [[ -x $f ]] && ok "exec $f" || bad "exec $f"  # -x tests whether the executable bit is set.
done

# 3. run.sh dispatch
section "run.sh"
check "run.sh help" ./run.sh help  # 'help' should exit 0 and print the banner.
check "run.sh list" ./run.sh list  # 'list' should exit 0 and list commands.
fails "run.sh unknown fails" ./run.sh __no_such_command__  # An unknown command should exit non-zero.
check "run.sh server status (no server)" ./run.sh server status  # 'server status' must work even when no server is running.

# 4. Layout constraint: only install.sh in root (plus run.sh/build helpers allowed)
section "layout"
[[ ! -f valhalla.sh ]] && ok "valhalla.sh moved" || bad "valhalla.sh still in root"  # The old root-level valhalla.sh must have been moved away.
[[ -f install.sh ]] && ok "install.sh in root" || bad "install.sh missing"  # install.sh must remain at the repository root.

# 5. qa lint
section "lint"
check "lint_sh" scripts/qa/lint_sh.sh  # Run the shell linter with no arguments.
check "check_init_sql_path" scripts/qa/check_init_sql_path.sh  # Run the init.sql path checker with no arguments.

echo
echo "Passed: $PASS  Failed: $FAIL"  # Print the final pass/fail counts.
# Exit 1 when any check failed so CI detects the regression; list exactly which checks failed.
if (( FAIL > 0 )); then printf '%s\n' "${FAILED[@]}"; exit 1; fi  # Exit 1 if anything failed so CI notices, and list the failing names.
