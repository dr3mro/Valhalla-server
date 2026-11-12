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
python3 -c 'import markdown' 2>/dev/null || python3 -m pip install --user markdown ||
	python3 -m pip install --user --break-system-packages markdown
python3 build_docs.py
echo "Rebuild the server (the page is embedded) and open /api/v2/doc"
