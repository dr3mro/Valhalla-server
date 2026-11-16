#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file fuzz_auth.sh
#  * @brief Launch the lightweight SQL-injection fuzzer for the provider/user create and login endpoints.
#  * @details This script launches a small SQL-injection fuzzer for the login endpoints.
#  * It finds the Python fuzzer next to this file, checks that python3 exists, and runs it.
#  * The target address can be changed with the BASE_URL environment variable.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is a quick security check for account creation and login.
#  * A developer runs it against a server they own, often during local development.
#  * Without it simple SQL injection mistakes in authentication could slip through to production.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash startup options: https://www.gnu.org/software/bash/manual/bash.html
#  * - The exec command: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Thin wrapper for tests/api_tests/fuzz_auth.py - the small, sqlmap-free SQLi fuzzer for the providers/users
# create and login endpoints. Run from anywhere; always resolves the script relative to this file.
#
# Usage: ./fuzz_auth.sh
# Env:   BASE_URL (default http://127.0.0.1:8080/api/v2)
# No `-e`: this is a thin launcher whose only failure paths are checked explicitly below.
set -uo pipefail  # Treat unset variables and failed pipeline steps as errors.

# Resolve the repo root from this file so the wrapper works from any directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # Absolute repository root, found from this file's own location.
# BASE_URL is caller-overridable so the fuzzer can target a non-default host/port.
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"  # Address to fuzz; defaults to a local server if the caller did not set one.
SCRIPT="$ROOT/tests/api_tests/fuzz_auth.py"  # Full path to the Python fuzzer we will run.

# Preflight the interpreter and the fuzzer with a distinct exit code 2 instead of a confusing traceback.
command -v python3 >/dev/null || { echo "error: 'python3' is required but was not found on PATH" >&2; exit 2; }  # Stop early with a clear message if Python is missing.
[[ -f $SCRIPT ]] || { echo "error: $SCRIPT not found" >&2; exit 2; }  # Stop early if the fuzzer file is missing.

# Only ever fuzz a server you own: the Python script sends intentionally malicious SQLi payloads.
# Replace this shell with Python so the fuzzer's exit code and signals become the wrapper's own.
exec python3 "$SCRIPT" "$BASE_URL"  # Hand over to Python so its exit code and signals become this wrapper's own.
