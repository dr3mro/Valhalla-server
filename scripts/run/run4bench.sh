#!/bin/sh

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_DIR="$ROOT/build/run"
mkdir -p "$RUN_DIR"
cd "$RUN_DIR" || exit 1

DB_WORKERS=5 PERMISSION_CACHE_TTL=900 SERVER_THREADS=4 DB_MAX_CONN=5 MAX_REQUESTS=10000000 MAX_FPS=3000000 exec "$ROOT/build/Release/server"
