#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file run_debug.sh
#  * @brief Load an env file (if present) and run the Debug server build, optionally rebuilding first.
#  * @details This script starts the Debug build of the server for everyday development.
#  * It chooses an env file, loads its settings, and can rebuild the binary first with --build.
#  * It then launches the server from build/run, passing along any extra arguments.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the launcher developers use while writing and debugging the C++ code.
#  * It keeps debug runs separate from release runs and lets each developer use their own configuration.
#  * Without it every developer would have to remember the right binary, folder and environment by hand.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash functions: https://www.gnu.org/software/bash/manual/bash.html
#  * - The exec command: https://www.gnu.org/software/bash/manual/bash.html
#  * - CMake build tool: https://cmake.org/cmake/help/latest/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Load .env (if present) and run the Debug build of the server.
# Usage: ./scripts/run/run_debug.sh [--build] [server args...]
# Abort on errors, unset variables or failed pipelines so a broken launch is not mistaken for a running server.
set -euo pipefail  # Stop on an error, an unset variable or a failed pipeline step.

# Resolve and enter build/run from the script's location so server-relative files land there.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # Absolute repository root, found from this file's own location.
RUN_DIR="$ROOT/build/run"  # Folder where the server keeps its run-time files.
mkdir -p "$RUN_DIR"  # Make sure the run folder exists before entering it.
cd "$RUN_DIR"  # Run the server from that folder so its files land there.

# An explicit ENV_FILE wins; otherwise prefer config, then .env, and fall back to the config path so the
# loader can report the missing file once instead of silently running with built-in defaults.
if [[ -z ${ENV_FILE:-} ]]; then  # If the caller did not pick an env file, choose one below.
  if [[ -f $ROOT/config ]]; then ENV_FILE="$ROOT/config"  # Prefer the config file when it exists.
  elif [[ -f $ROOT/.env ]]; then ENV_FILE="$ROOT/.env"  # Otherwise use .env if it exists.
  else ENV_FILE="$ROOT/config"  # Otherwise point at config so the loader can report the missing file once.
  fi
fi
# Point at the Debug binary; its existence is checked after any requested rebuild.
BIN="$ROOT/build/Debug/server"  # Path to the Debug server binary we intend to run.

# Reuse run_release's env loader by sourcing its function? Instead duplicate minimal loader.
# @brief Parse KEY=VALUE lines from an env file and export the resulting variables.
# @param $1 Path to the env file; a missing file is a no-op that only prints a note on stderr.
# @return 0 (the function always succeeds so the caller can continue with the current environment).
# @note Parsed, never sourced, so file contents cannot execute as shell code; tolerates comments,
#       blank lines, optional `export`, surrounding spaces, matching quotes and CRLF endings.
load_env() {
    local line key val  # Local variables: one file line, a variable name and its value.
    [[ -f $1 ]] || { echo "note: no env file at $1, using existing environment" >&2; return 0; }  # If there is no env file, just print a note and keep going.
    while IFS= read -r line || [[ -n $line ]]; do  # Read every line, including a final line that has no newline at the end.
        line=${line%$'\r'}  # Remove a Windows-style carriage return from the end of the line.
        # Skip blank lines and lines that start with # (comments in the env file).
        [[ $line =~ ^[[:space:]]*(#|$) ]] && continue
        [[ $line =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue  # Keep only KEY=VALUE lines, optionally starting with the word export.
        key=${BASH_REMATCH[2]}  # The variable name captured by the pattern above.
        val=${BASH_REMATCH[3]}  # The value captured by the pattern above.
        val=${val%"${val##*[![:space:]]}"}  # Trim trailing whitespace from the value.
        if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then  # If the value is wrapped in matching quotes, strip them.
            val=${BASH_REMATCH[1]}  # Keep only the text that was inside the quotes.
        fi
        export "$key=$val"  # Export the pair so the server process will inherit it.
    done <"$1"  # Feed the env file into the reading loop.
}

# `--build` is consumed here; everything after it is forwarded verbatim to the server.
if [[ ${1:-} == "--build" ]]; then
    shift  # Remove the --build flag now that it has been handled.
    cmake --build "$ROOT/build/Debug"  # Rebuild the Debug binary before running it.
fi

# Refuse to continue without an executable binary rather than letting exec fail cryptically.
[[ -x $BIN ]] || { echo "error: $BIN not found. Run scripts/build/build.sh or use --build" >&2; exit 1; }

# Export the env file before exec so the server inherits every setting.
load_env "$ENV_FILE"  # Load the chosen settings into the environment before launch.
# Replace this shell with the server: exec forwards signals and exit status directly, and "$@" passes remaining args.
exec "$BIN" "$@"  # Replace this shell with the server and pass through any remaining arguments.
