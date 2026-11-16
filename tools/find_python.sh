#!/usr/bin/env bash
# Shared helper: locate a usable python3 (prefer Homebrew on macOS, then PATH).
set -euo pipefail

for cand in python3 python /opt/homebrew/bin/python3 /usr/local/bin/python3; do
    if command -v "$cand" >/dev/null 2>&1; then
        resolved="$(command -v "$cand")"
        if "$resolved" --version >/dev/null 2>&1; then
            printf '%s\n' "$resolved"
            exit 0
        fi
    fi
done

echo "python3 not found" >&2
exit 1