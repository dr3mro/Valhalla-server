#!/usr/bin/env bash
# Regenerates docs/api.html, docs/openapi.json and docs/valhalla.postman_collection.json.
# Needs: node/npm (swagger-ui-dist), python3 with the "markdown" package.
#   SAMPLES_OUT=api_tests/samples.jsonl ./test_api.sh   # optional, adds real response samples
#   tools/docs/build.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
if [[ ! -d node_modules/swagger-ui-dist ]]; then
	command -v npm >/dev/null || {
		cat >&2 <<'MSG'
error: npm not found (Node.js is only needed to fetch swagger-ui-dist).
  macOS:          brew install node
  Debian/Ubuntu:  sudo apt install nodejs npm
  Fedora:         sudo dnf install nodejs npm
MSG
		exit 1
	}
	npm install --no-audit --no-fund
fi
command -v python3 >/dev/null || { echo "error: python3 not found" >&2; exit 1; }
PY=python3
if ! python3 -c 'import markdown' 2>/dev/null; then
	# Externally-managed Pythons (Homebrew, Debian 12+, Fedora) refuse pip installs: use a local venv.
	[[ -x .venv/bin/python ]] || python3 -m venv .venv
	.venv/bin/python -m pip install --quiet markdown
	PY=.venv/bin/python
fi
"$PY" build_docs.py
echo "Docs generated. cmake --build runs this script automatically; otherwise rebuild the server to embed them."
