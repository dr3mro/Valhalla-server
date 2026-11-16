#!/usr/bin/env bash
# /**
#  * @file check_format.sh
#  * @brief Check clang-format compliance of staged or tracked C/C++ files, judging only changed lines when clang-format-diff is available.
#  * @details Verify that C/C++ files follow clang-format rules, judging only changed lines when the clang-format-diff helper is installed.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * It runs as a pre-commit hook and in CI to stop badly formatted code from being merged.
#  * Checking only changed lines means old formatting mistakes do not block unrelated work.
#  * Without it, style would drift and code reviews would waste time on whitespace.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - clang-format: https://clang.llvm.org/docs/ClangFormat.html
#  * - git diff: https://git-scm.com/docs/git-diff
#  * - grep: https://www.gnu.org/software/grep/manual/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Check clang-format on staged (or all tracked) C/C++ files.
# Uses clang-format-diff when available so only changed lines are judged
# (pre-existing violations in untouched code do not fail the check).
# Usage: tools/check_format.sh            check every tracked C/C++ file
#        tools/check_format.sh --staged  check only staged C/C++ files
# -u and pipefail catch typos and hidden pipeline errors; -e is omitted because status is accumulated and reported at exit.
set -uo pipefail  # u = error on unset variables, pipefail = catch pipeline failures; no -e because status is gathered at the end.

# Run from the repository root so git pathspecs and file paths resolve consistently.
cd "$(git rev-parse --show-toplevel)" || exit 1  # || exit 1 stops immediately if the repository root cannot be found.

# Locate clang-format.
# Exit 2 signals a missing tool (distinct from 1 = formatting violations) so callers can tell the two apart.
if command -v clang-format >/dev/null 2>&1; then  # command -v finds a program on PATH without actually running it.
    clang_format=clang-format  # Save the command name once so later code can reuse it.
elif [[ -x /opt/homebrew/opt/llvm/bin/clang-format ]]; then
    clang_format=/opt/homebrew/opt/llvm/bin/clang-format  # Homebrew installs LLVM tools here on Apple Silicon Macs.
else
    echo "error: clang-format not found" >&2
    exit 2  # Exit 2 means 'tool missing', which is different from exit 1 = 'formatting wrong'.
fi

# Locate clang-format-diff.py.
# An empty diff_tool means "unavailable", which triggers the whole-file fallback below.
diff_tool=""  # An empty value means clang-format-diff is unavailable, so use the whole-file fallback.
if command -v clang-format-diff.py >/dev/null 2>&1; then
    diff_tool="$(command -v clang-format-diff.py)"  # command -v returns the full path to the tool.
elif [[ -x "$HOME/.local/bin/clang-format-diff.py" ]]; then
    diff_tool="$HOME/.local/bin/clang-format-diff.py"
elif [[ -x /Library/Developer/CommandLineTools/usr/bin/clang-format-diff.py ]]; then
    diff_tool=/Library/Developer/CommandLineTools/usr/bin/clang-format-diff.py
elif [[ -x /usr/bin/clang-format-diff.py ]]; then
    diff_tool=/usr/bin/clang-format-diff.py
fi

# The repository's .clang-format is the single source of truth for style.
# Fail early if it is missing, so QA never silently falls back to a built-in style.
if [[ ! -f .clang-format ]]; then  # -f checks for a regular file at the repository root.
    echo "error: .clang-format not found at repository root" >&2
    exit 2  # Exit 2 means 'configuration missing', distinct from a formatting violation.
fi
style_args=(--style=file)  # --style=file makes clang-format read the repository's .clang-format.
diff_style_args=(-style file -binary "$clang_format")  # Pass the same config and binary to clang-format-diff.py.

# These globs are used as git pathspecs, limiting the check to C/C++ sources and headers.
paths=('*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')  # These patterns become git pathspecs so only C/C++ files are examined.

# Choose the comparison base: the index with --staged, otherwise HEAD.
if [[ ${1:-} == "--staged" ]]; then  # ${1:-} means 'first argument or empty', which is safe under -u.
    diff_base=(git diff --cached)  # --cached = changes staged for commit.
else
    diff_base=(git diff HEAD)  # Without --staged, compare the working tree to HEAD instead of the index.
fi

if [[ -n $diff_tool ]]; then
    # clang-format-diff.py cannot judge hunks that start at old line 0 (@@ -0,0
    # or @@ -0,0 -> new content). It asks clang-format for -lines=0:N, which
    # exits with "start line should be at least 1". Such files are checked as
    # whole files with --dry-run --Werror instead; all other hunks go through
    # clang-format-diff so only changed lines are judged.
    changed_files=()  # C/C++ files changed relative to the comparison base.
    while IFS= read -r -d '' f; do changed_files+=("$f"); done < <("${diff_base[@]}" --name-only -z --diff-filter=ACM -- "${paths[@]}")

    result=""  # Any text here is a formatting problem to report.
    diff_for_tool=""  # Accumulated diff of files with line-numbered hunks.
    if [[ ${#changed_files[@]} -gt 0 ]]; then  # Guard for bash 3.2 + 'set -u' expanding an empty array.
        for f in "${changed_files[@]}"; do
            file_diff="$("${diff_base[@]}" -U0 -- "$f")"  # Diff for this file only; -U0 = no context lines.
            [[ -z $file_diff ]] && continue  # Skip files with no changes.
            if printf '%s\n' "$file_diff" | grep '^@@ -0' >/dev/null; then  # Hunk starts at old line 0 -> new/empty file. grep consumes all input so printf cannot get EPIPE.
                if ! "$clang_format" "${style_args[@]}" --dry-run --Werror "$f" >/dev/null 2>&1; then  # Check the whole file.
                    result+="clang-format: $f needs formatting"$'\n'  # Record the file as needing formatting.
                fi
            else
                diff_for_tool+="$file_diff"$'\n'  # Safe to pass to clang-format-diff.py.
            fi
        done
    fi
    if [[ -n $diff_for_tool ]]; then  # Only invoke clang-format-diff when there is a valid diff.
        tool_output="$(printf '%s\n' "$diff_for_tool" | "$diff_tool" -p1 "${diff_style_args[@]}" 2>&1)"  # -p1 strips a/ b/ prefixes; style args force .clang-format.
        [[ -n $tool_output ]] && result+="$tool_output"$'\n'  # Any output is a patch of lines needing formatting.
    fi
    if [[ -n $result ]]; then  # Any output is a patch of lines that still need formatting.
        echo "clang-format: changed lines need formatting" >&2
        printf '%s\n' "$result" >&2
        exit 1
    fi
    exit 0
fi

if [[ ${1:-} == "--staged" ]]; then  # Fallback whole-file check when clang-format-diff is unavailable.
    files="$(git diff --cached --name-only --diff-filter=ACM -- "${paths[@]}")"  # --diff-filter=ACM keeps only added, copied and modified files.
else
    files="$(git ls-files "${paths[@]}")"  # git ls-files lists every tracked file matching the C/C++ patterns.
fi

# Fallback: whole-file check (only when clang-format-diff is unavailable).
# Accumulate status instead of using -e so every offending file is reported before exiting.
status=0  # Start optimistic; this becomes 1 if any file fails.
while IFS= read -r f; do  # IFS= read -r preserves each filename exactly, even if it contains spaces.
    [[ -n $f ]] || continue  # Skip empty lines produced by the here-string.
    [[ -f $f ]] || continue  # Skip names that are no longer ordinary files (for example, deleted files).
    # --dry-run leaves the file untouched; --Werror makes violations return a non-zero status.
    if ! "$clang_format" "${style_args[@]}" --dry-run --Werror "$f" >/dev/null 2>&1; then  # --dry-run checks without editing; --Werror makes violations return a non-zero status.
        echo "clang-format: $f needs formatting" >&2
        # Re-run with diagnostics on stderr; `|| true` keeps this display from overwriting $status.
        "$clang_format" "${style_args[@]}" --dry-run --Werror "$f" >&2 || true  # || true stops this display-only run from overwriting the saved status.
        status=1
    fi
done <<<"$files"  # <<< feeds the file list into the loop one line at a time.
exit "$status"  # Return the accumulated status to the caller (CI or the git hook).
