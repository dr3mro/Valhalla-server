#!/usr/bin/env bash
# Load .env (if present) and run the Debug build of the server.
# Usage: ./scripts/run/run_debug.sh [--build] [server args...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if [[ -z ${ENV_FILE:-} ]]; then
  if [[ -f $ROOT/config ]]; then ENV_FILE="$ROOT/config"
  elif [[ -f $ROOT/.env ]]; then ENV_FILE="$ROOT/.env"
  else ENV_FILE="$ROOT/config"
  fi
fi
BIN="$ROOT/build/Debug/server"

# Reuse run_release's env loader by sourcing its function? Instead duplicate minimal loader.
load_env() {
    local line key val
    [[ -f $1 ]] || { echo "note: no env file at $1, using existing environment" >&2; return 0; }
    while IFS= read -r line || [[ -n $line ]]; do
        line=${line%$'\r'}
        [[ $line =~ ^[[:space:]]*(#|$) ]] && continue
        [[ $line =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue
        key=${BASH_REMATCH[2]}
        val=${BASH_REMATCH[3]}
        val=${val%"${val##*[![:space:]]}"}
        if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then
            val=${BASH_REMATCH[1]}
        fi
        export "$key=$val"
    done <"$1"
}

if [[ ${1:-} == "--build" ]]; then
    shift
    cmake --build "$ROOT/build/Debug"
fi

[[ -x $BIN ]] || { echo "error: $BIN not found. Run scripts/build/build.sh or use --build" >&2; exit 1; }

load_env "$ENV_FILE"
exec "$BIN" "$@"
