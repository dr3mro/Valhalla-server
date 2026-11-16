#!/usr/bin/env bash
# Hygiene checks on staged files: conflict markers, trailing whitespace,
# missing final newline, oversized files.
# Usage: tools/check_hygiene.sh            check all tracked files
#        tools/check_hygiene.sh --staged   check only staged files
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

max_size=1048576  # 1 MiB

if [[ ${1:-} == "--staged" ]]; then
    files="$(git diff --cached --name-only --diff-filter=ACM)"
else
    files="$(git ls-files)"
fi

status=0
while IFS= read -r f; do
    [[ -n $f ]] || continue
    [[ -f $f ]] || continue

    # Skip binary files.
    if git diff --numstat -- "$f" >/dev/null 2>&1; then
        :
    fi
    if file "$f" 2>/dev/null | grep -q 'binary'; then
        continue
    fi

    # Oversized file check.
    size=$(wc -c <"$f" | tr -d ' ')
    if [[ $size -gt $max_size ]]; then
        echo "hygiene: $f is larger than 1 MiB ($size bytes)" >&2
        status=1
        continue
    fi

    # Conflict markers.
    if grep -nE '^(<<<<<<<|=======|>>>>>>>)' "$f" >/dev/null 2>&1; then
        echo "hygiene: $f contains merge-conflict markers" >&2
        grep -nE '^(<<<<<<<|=======|>>>>>>>)' "$f" >&2 || true
        status=1
    fi

    # Trailing whitespace.
    if grep -nE '[[:blank:]]+$' "$f" >/dev/null 2>&1; then
        echo "hygiene: $f has trailing whitespace" >&2
        grep -nE '[[:blank:]]+$' "$f" >&2 || true
        status=1
    fi

    # Missing final newline (only for text files).
    if [[ -s $f ]]; then
        last=$(tail -c 1 "$f" | od -An -tuC | tr -d ' ')
        if [[ $last != 10 ]]; then
            echo "hygiene: $f is missing a final newline" >&2
            status=1
        fi
    fi
done <<<"$files"

exit "$status"
