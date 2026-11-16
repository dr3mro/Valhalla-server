#!/usr/bin/env bash
# Check clang-format on staged (or all tracked) C/C++ files.
# Usage: tools/check_format.sh            check every tracked C/C++ file
#        tools/check_format.sh --staged  check only staged C/C++ files
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

if ! command -v clang-format >/dev/null 2>&1; then
    if [[ -x /opt/homebrew/opt/llvm/bin/clang-format ]]; then
        clang_format=/opt/homebrew/opt/llvm/bin/clang-format
    else
        echo "error: clang-format not found" >&2
        exit 2
    fi
else
    clang_format=clang-format
fi

if [[ ${1:-} == "--staged" ]]; then
    files="$(git diff --cached --name-only --diff-filter=ACM -- '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')"
else
    files="$(git ls-files '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')"
fi

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
