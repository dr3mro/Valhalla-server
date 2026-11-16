#!/usr/bin/env bash
# /**
#  * @file check_hygiene.sh
#  * @brief Check staged or tracked text files for merge-conflict markers, trailing whitespace, missing final newlines, and files over 1 MiB.
#  * @details Scan text files for merge-conflict markers, trailing spaces, a missing final newline, and files larger than 1 MiB.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * It runs in CI and as a pre-commit hook to keep the repository tidy and merge-safe.
#  * Conflict markers left in a file can ship broken code to users.
#  * Without it, whitespace noise and half-resolved merges would creep into commits.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - grep: https://www.gnu.org/software/grep/manual/
#  * - git diff: https://git-scm.com/docs/git-diff
#  * - POSIX shell conditionals: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Hygiene checks on staged files: conflict markers, trailing whitespace,
# missing final newline, oversized files.
# Usage: tools/check_hygiene.sh            check all tracked files
#        tools/check_hygiene.sh --staged   check only staged files
# -u and pipefail catch typos and hidden pipeline errors; -e is omitted so status can be accumulated across all files.
set -uo pipefail  # u and pipefail catch mistakes; -e is omitted so all problems can be reported in one pass.

# Run from the repository root so git's file list and paths resolve consistently.
cd "$(git rev-parse --show-toplevel)" || exit 1  # || exit 1 stops if the script is not run from inside a git repository.

max_size=1048576  # 1 MiB  # 1024 x 1024 bytes = 1 MiB; bigger files are rejected.

if [[ ${1:-} == "--staged" ]]; then  # ${1:-} avoids an unset-variable error under -u when no argument is given.
    # --diff-filter=ACM keeps added/copied/modified files, the only ones that can introduce hygiene issues.
    files="$(git diff --cached --name-only --diff-filter=ACM)"  # Only added, copied or modified files can introduce new hygiene problems.
else
    files="$(git ls-files)"  # git ls-files lists all tracked files when --staged is not used.
fi

# Accumulate status so every hygiene problem is reported in a single pass before exiting.
status=0  # Start at 0 (clean) and flip to 1 when the first problem is found.
while IFS= read -r f; do  # Read one filename at a time without mangling spaces.
    [[ -n $f ]] || continue  # Ignore empty entries in the file list.
    [[ -f $f ]] || continue  # Ignore names that are no longer ordinary files.

    # Generated output (Doxygen site, bundled API page) is written by tools, so
    # whitespace and size rules do not apply to it.
    case $f in
        docs/doxygen/* | docs/api.html) continue ;;
    esac

    # Skip binary files.
    # NOTE: the git diff probe below has no side effects; the `file` test that follows is the real binary check.
    if git diff --numstat -- "$f" >/dev/null 2>&1; then
        :
    fi
    if file "$f" 2>/dev/null | grep -q 'binary'; then  # The file command reports 'binary' for non-text data, which should be skipped.
        continue  # Continue to the next file without running text checks on it.
    fi

    # Oversized file check.
    # tr strips the padding some `wc` implementations add before the byte count.
    size=$(wc -c <"$f" | tr -d ' ')  # wc -c counts bytes; tr removes padding spaces some versions add.
    if [[ $size -gt $max_size ]]; then  # -gt means 'greater than' the size limit.
        echo "hygiene: $f is larger than 1 MiB ($size bytes)" >&2
        status=1
        continue  # One oversized file is enough; move on to the next file.
    fi

    # Conflict markers.
    if grep -nE '^(<<<<<<<|=======|>>>>>>>)' "$f" >/dev/null 2>&1; then  # grep -nE prints matching lines with their numbers, using extended regex.
        echo "hygiene: $f contains merge-conflict markers" >&2
        grep -nE '^(<<<<<<<|=======|>>>>>>>)' "$f" >&2 || true  # Show the offending lines; || true keeps grep's status from changing the script status.
        status=1
    fi

    # Trailing whitespace.
    if grep -nE '[[:blank:]]+$' "$f" >/dev/null 2>&1; then  # [[:blank:]]+$ means a space or tab at the very end of a line.
        echo "hygiene: $f has trailing whitespace" >&2
        grep -nE '[[:blank:]]+$' "$f" >&2 || true
        status=1
    fi

    # Missing final newline (only for text files).
    # -s skips empty files; od -tuC gives the final byte's decimal value, compared with 10 (LF).
    if [[ -s $f ]]; then
        last=$(tail -c 1 "$f" | od -An -tuC | tr -d ' ')  # tail -c 1 reads the last byte; od prints it as a decimal number.
        if [[ $last != 10 ]]; then  # 10 is the ASCII/UTF-8 code for a newline (line feed).
            echo "hygiene: $f is missing a final newline" >&2
            status=1
        fi
    fi
done <<<"$files"  # Feed the whole file list into the loop.

exit "$status"  # Final status: 0 = clean, 1 = at least one hygiene problem found.
