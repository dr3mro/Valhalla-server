#!/usr/bin/env bash
# Check clang-format on staged (or all tracked) C/C++ files.
# Uses clang-format-diff when available so only changed lines are judged
# (pre-existing violations in untouched code do not fail the check).
# Usage: tools/check_format.sh            check every tracked C/C++ file
#        tools/check_format.sh --staged  check only staged C/C++ files
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

# Locate clang-format.
if command -v clang-format >/dev/null 2>&1; then
    clang_format=clang-format
elif [[ -x /opt/homebrew/opt/llvm/bin/clang-format ]]; then
    clang_format=/opt/homebrew/opt/llvm/bin/clang-format
else
    echo "error: clang-format not found" >&2
    exit 2
fi

# Locate clang-format-diff.py.
diff_tool=""
if command -v clang-format-diff.py >/dev/null 2>&1; then
    diff_tool="$(command -v clang-format-diff.py)"
elif [[ -x "$HOME/.local/bin/clang-format-diff.py" ]]; then
    diff_tool="$HOME/.local/bin/clang-format-diff.py"
elif [[ -x /Library/Developer/CommandLineTools/usr/bin/clang-format-diff.py ]]; then
    diff_tool=/Library/Developer/CommandLineTools/usr/bin/clang-format-diff.py
elif [[ -x /usr/bin/clang-format-diff.py ]]; then
    diff_tool=/usr/bin/clang-format-diff.py
fi

paths=('*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')

if [[ ${1:-} == "--staged" ]]; then
    if [[ -n $diff_tool ]]; then
        diff_output="$(git diff --cached -U0 -- "${paths[@]}")"
        if [[ -z $diff_output ]]; then
            exit 0
        fi
        result="$(printf '%s\n' "$diff_output" | "$diff_tool" -p1 2>&1)"
        if [[ -n $result ]]; then
            echo "clang-format: changed lines need formatting" >&2
            printf '%s\n' "$result" >&2
            exit 1
        fi
        exit 0
    fi
    files="$(git diff --cached --name-only --diff-filter=ACM -- "${paths[@]}")"
else
    if [[ -n $diff_tool ]]; then
        diff_output="$(git diff -U0 HEAD -- "${paths[@]}")"
        if [[ -z $diff_output ]]; then
            exit 0
        fi
        result="$(printf '%s\n' "$diff_output" | "$diff_tool" -p1 2>&1)"
        if [[ -n $result ]]; then
            echo "clang-format: changed lines need formatting" >&2
            printf '%s\n' "$result" >&2
            exit 1
        fi
        exit 0
    fi
    files="$(git ls-files "${paths[@]}")"
fi

# Fallback: whole-file check (only when clang-format-diff is unavailable).
status=0
while IFS= read -r f; do
    [[ -n $f ]] || continue
    [[ -f $f ]] || continue
    if ! "$clang_format" --dry-run --Werror "$f" >/dev/null 2>&1; then
        echo "clang-format: $f needs formatting" >&2
        "$clang_format" --dry-run --Werror "$f" >&2 || true
        status=1
    fi
done <<<"$files"
exit "$status"
