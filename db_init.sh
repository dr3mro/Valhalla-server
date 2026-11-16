#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
psql -h 127.0.0.1 -U postgres -d postgres -f "$SCRIPT_DIR/init.sql"