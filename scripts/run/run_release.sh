#!/usr/bin/env bash
# Load .env (if present) and run the Release build of the server.
# Usage: ./run_release.sh [--build] [server args...]
#   --build   (re)build build/Release before running
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN_DIR="$ROOT/build/run"
mkdir -p "$RUN_DIR"
cd "$RUN_DIR"

if [[ -z ${ENV_FILE:-} ]]; then
  if [[ -f $ROOT/config ]]; then ENV_FILE="$ROOT/config"
  elif [[ -f $ROOT/.env ]]; then ENV_FILE="$ROOT/.env"
  else ENV_FILE="$ROOT/config"
  fi
fi
BIN="$ROOT/build/Release/server"

# Parse KEY=VALUE lines (tolerates spaces around '=', quotes, CRLF, comments).
# Not sourced, so values are never executed as shell code.
load_env() {
	local line key val
	[[ -f $1 ]] || { echo "note: no env file at $1, using existing environment" >&2; return 0; }
	while IFS= read -r line || [[ -n $line ]]; do
		line=${line%$'\r'}
		[[ $line =~ ^[[:space:]]*(#|$) ]] && continue
		[[ $line =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue
		key=${BASH_REMATCH[2]}
		val=${BASH_REMATCH[3]}
		val=${val%"${val##*[![:space:]]}"} # trim trailing whitespace
		if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then
			val=${BASH_REMATCH[1]}
		fi
		export "$key=$val"
	done <"$1"
}

if [[ ${1:-} == "--build" ]]; then
	shift
	cmake --build build/Release
fi

[[ -x $BIN ]] || { echo "error: $BIN not found. Run scripts/build/build.sh or use --build" >&2; exit 1; }

load_env "$ENV_FILE"
exec "$BIN" "$@"
