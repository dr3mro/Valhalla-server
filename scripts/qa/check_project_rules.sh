#!/usr/bin/env bash
# /**
#  * @file check_project_rules.sh
#  * @brief Enforce project coding rules on added lines of staged or tracked C++ files (see aync_plan.md), with per-file exemptions.
#  * @details Ban direct console output, raw environment access and uncached Store lookups on newly added lines of C++ code.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * The project wants logs to go through its logger and environment reads through envloader.
#  * It runs in CI and pre-commit so new code follows these rules while old code is left alone.
#  * Without it, inconsistent logging and hidden global reads would spread through the codebase.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - grep: https://www.gnu.org/software/grep/manual/
#  * - git diff: https://git-scm.com/docs/git-diff
#  * - Bash arrays: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Enforce project coding rules on staged C++ files (see aync_plan.md).
# Usage: tools/check_project_rules.sh            check all tracked C++ files (added lines vs HEAD)
#        tools/check_project_rules.sh --staged   check only staged C++ files
# -u and pipefail catch typos and hidden pipeline errors; -e is omitted because status is accumulated across all violations.
set -uo pipefail  # u and pipefail catch mistakes; no -e so all violations can be listed together.

# Run from the repository root so git's file list and paths resolve consistently.
cd "$(git rev-parse --show-toplevel)" || exit 1  # Run from the repository root so git's file paths are reliable.

if [[ ${1:-} == "--staged" ]]; then  # Choose staged files when the --staged flag was passed.
    # --diff-filter=ACM keeps added/copied/modified files, the only ones that can introduce rule violations.
    files="$(git diff --cached --name-only --diff-filter=ACM -- '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')"  # Only C/C++ files, and only additions/copies/modifications, can add rule violations.
else
    files="$(git ls-files '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')"  # Otherwise list every tracked C/C++ file.
fi

# Accumulate status so every offending line is reported before exiting.
status=0  # Start at 0 (no violations found yet).
while IFS= read -r f; do  # Process one file per loop iteration.
    [[ -n $f ]] || continue
    [[ -f $f ]] || continue

    # Per-file exemptions: these are the only files allowed to use the otherwise forbidden APIs.
    allow_getenv=0  # 1 means this file may call getenv; 0 means it is forbidden.
    allow_cerr=0  # 1 means this file may write to cerr; 0 means it is forbidden.
    allow_cout=0  # 1 means this file may write to cout; 0 means it is forbidden.
    case "$f" in  # case selects the exemption, if any, for this particular file.
        src/configurator/envloader/envvars/envvars.cpp) allow_getenv=1 ;;  # The env loader is the one place allowed to read environment variables.
        src/main.cpp) allow_cerr=1 ;;  # Early startup code may use cerr before the logger exists.
        src/server/server.cpp) allow_cout=1 ;;  # The main server startup file is allowed to use cout.
    esac

    # Grandfather APIs that the file already used in HEAD: reformatting an old
    # line makes it appear as "added", but it is not a newly introduced violation.
    head_content=""
    if git cat-file -e "HEAD:$f" 2>/dev/null; then
        head_content="$(git show "HEAD:$f")"  # Read the committed version for comparison.
    fi
    head_console=0  # 1 means HEAD already used direct console output.
    head_env=0  # 1 means HEAD already used getenv/setenv.
    head_store=0  # 1 means HEAD already used a non-cached Store:: lookup.
    printf '%s' "$head_content" | grep -E 'std::cout|std::cerr|[[:<:]]printf[[:space:]]*\(|[[:<:]]puts[[:space:]]*\(|fmt::print' >/dev/null && head_console=1
    printf '%s' "$head_content" | grep -E 'getenv|setenv' >/dev/null && head_env=1
    if printf '%s' "$head_content" | grep 'Store::' | grep -v 'Store::cachedObject' >/dev/null; then  # Any non-cached lookup in HEAD grandfathers this file.
        head_store=1
    fi

    # Build the diff command as an array so the path is passed as a single argument, even if it contains spaces.
    if [[ ${1:-} == "--staged" ]]; then
        diff_cmd=(git diff --cached -U0 -- "$f")  # In staged mode, diff the index against HEAD.
    else
        # For non-staged mode, compare working tree to HEAD.
        diff_cmd=(git diff -U0 HEAD -- "$f")
    fi  # In normal mode, diff the working tree against HEAD.

    while IFS= read -r line; do
        # Strip the leading '+' that marks an added line in the diff.
        code="${line#+}"  # In a diff, a leading '+' marks an added line, so strip it to get the code.
        # Bash idiom: strip leading whitespace so blank lines and comments can be detected.
        trimmed="${code#"${code%%[![:space:]]*}"}"  # Remove leading whitespace so blank lines and comments can be detected.
        [[ -z $trimmed ]] && continue  # Skip blank lines; they cannot violate a rule.
        # All comment forms are exempt: // line comments and /** ... */ blocks
        # (whose lines start with '*'). A comment merely mentioning a forbidden
        # API is documentation, not a call to it.
        case "$trimmed" in //*|'/*'*|'*/'|'*'*) continue ;; esac

        file_ok=1  # Assume the line is fine until a rule marks it as bad.
        # [[:<:]] is a BSD word boundary, so e.g. myprintf is not mistaken for printf.
        # stdout/stderr/printf/puts/fmt::print
        if [[ $head_console -eq 0 ]] && printf '%s' "$code" | grep -E 'std::cout|std::cerr|[[:<:]]printf[[:space:]]*\(|[[:<:]]puts[[:space:]]*\(|fmt::print' >/dev/null; then  # Direct console output should use the project logger instead.
            if [[ $code == *std::cout* && $allow_cout -eq 1 ]]; then  # Allow cout only in the exempt main.cpp.
                :
            elif [[ $code == *std::cerr* && $allow_cerr -eq 1 ]]; then  # Allow cerr only in the exempt startup file.
                :
            else
                file_ok=0  # Any other console output is a violation.
            fi
        fi
        # getenv / setenv outside envloader
        if [[ $head_env -eq 0 ]] && printf '%s' "$code" | grep -E 'getenv|setenv' >/dev/null; then  # Environment reads are centralized in the env loader.
            if [[ $allow_getenv -eq 0 ]]; then  # Outside the exempt file, getenv/setenv is not allowed.
                file_ok=0
            fi
        fi
        # Store:: lookups (cachedObject is allowed)
        if [[ $head_store -eq 0 ]] && printf '%s' "$code" | grep 'Store::' >/dev/null; then  # Direct Store:: lookups bypass the approved cache helper.
            if ! printf '%s' "$code" | grep 'Store::cachedObject' >/dev/null; then  # Store::cachedObject is the approved lookup and is allowed.
                file_ok=0  # Any other Store:: call is a violation.
            fi
        fi

        if [[ $file_ok -eq 0 ]]; then  # Report the line once if any rule failed.
            echo "project-rules: $f: $trimmed" >&2  # Print the file and code so the developer can fix it.
            status=1  # Count at least one violation.
        fi
    # Keep only added lines; grep -v '^+++' drops the diff file header, which also begins with '+'.
    done < <("${diff_cmd[@]}" | grep '^+' | grep -v '^+++')  # Keep added lines only; '+++' is the diff header, not code.
done <<<"$files"  # Feed the file list into the loop.

exit "$status"  # The number of violations becomes the exit status.
