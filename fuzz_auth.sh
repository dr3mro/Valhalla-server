#!/usr/bin/env bash
# Thin wrapper for api_tests/fuzz_auth.py - the small, sqlmap-free SQLi fuzzer for the providers/users
# create and login endpoints. Run from anywhere; always resolves the script relative to this file.
#
# Usage: ./fuzz_auth.sh
# Env:   BASE_URL (default http://127.0.0.1:8080/api/v2)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"
SCRIPT="$ROOT/api_tests/fuzz_auth.py"

command -v python3 >/dev/null || { echo "error: 'python3' is required but was not found on PATH" >&2; exit 2; }
[[ -f $SCRIPT ]] || { echo "error: $SCRIPT not found" >&2; exit 2; }

exec python3 "$SCRIPT" "$BASE_URL"
