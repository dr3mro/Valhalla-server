#!/usr/bin/env bash
# /**
#  * @file lint_sh.sh
#  * @brief Lint staged or tracked shell scripts with ShellCheck (bash vs POSIX dialect from the shebang), plus dash -n for POSIX scripts, mirroring CI.
#  * @details Run ShellCheck on shell scripts, using bash rules for bash scripts and POSIX rules plus dash -n for the rest.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * It mirrors CI so developers can catch the same problems locally before pushing.
#  * The pre-commit hook calls it with --staged to check only files being committed.
#  * Without it, subtle quoting and portability bugs would reach the repository.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - ShellCheck: https://www.shellcheck.net/
#  * - dash (POSIX shell): https://www.debian.org/doc/debian-policy/ch-opersys.html
#  * - Bash shebangs: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Lint shell scripts exactly like CI (.github/workflows/scripts.yml): bash scripts with
# `shellcheck -s bash`, everything else with `shellcheck -s sh` plus `dash -n`.
# Usage: tools/lint_sh.sh            lint every tracked *.sh
#        tools/lint_sh.sh --staged   lint only the staged *.sh files (used by .githooks/pre-commit)
# Needs: shellcheck and dash (macOS: brew install shellcheck dash).
# -u and pipefail catch typos and hidden pipeline errors; -e is omitted because status is accumulated across all scripts.
set -uo pipefail  # u and pipefail catch mistakes; no -e so every script's findings can be collected.

# Run from the repository root so git's file list and paths resolve consistently.
cd "$(git rev-parse --show-toplevel)" || exit 1  # Run from the repository root so the '*.sh' pattern matches the right files.

# Exit 2 denotes a missing prerequisite, distinct from exit 1 for actual lint findings.
for tool in shellcheck dash; do  # Check that both required tools exist before doing any work.
    command -v "$tool" >/dev/null 2>&1 || {  # command -v looks up a program without running it.
        echo "error: $tool not found (macOS: brew install shellcheck dash)" >&2
        exit 2  # Exit 2 means 'missing tool', not 'lint found a problem'.
    }
done

if [[ ${1:-} == "--staged" ]]; then  # --staged selects only scripts that are about to be committed.
    # --diff-filter=ACM keeps added/copied/modified scripts, the only ones that can introduce lint errors.
    files="$(git diff --cached --name-only --diff-filter=ACM -- '*.sh')"  # Only added, copied or modified scripts can introduce new lint errors.
else
    files="$(git ls-files '*.sh')"  # Otherwise lint every tracked shell script.
fi

# Accumulate status so every offending script is reported before exiting.
status=0  # Start clean; flip to 1 when any script has findings.
while IFS= read -r f; do  # Process one script per loop iteration.
    [[ -n $f ]] || continue  # Skip empty lines in the file list.
    # Select the lint dialect from the shebang; non-bash scripts also get `dash -n` for a strict POSIX syntax check.
    case "$(head -n 1 "$f")" in  # The shebang (first line) decides which shell dialect to use.
        *bash*) shellcheck -s bash "$f" || status=1 ;;  # Bash scripts are checked with bash-specific rules.
        # `&&` binds tighter than `||`: status=1 only if shellcheck or dash fails, not when both pass.
        *) shellcheck -s sh "$f" && dash -n "$f" || status=1 ;;  # Other scripts also get a strict POSIX syntax check from dash.
    esac
done <<<"$files"  # Feed the file list into the loop.
exit "$status"  # 0 = all clean, 1 = at least one script had findings.
