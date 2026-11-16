#!/usr/bin/env bash
# /**
#  * @file build.sh
#  * @brief Regenerates docs/api.html, docs/openapi.json and docs/valhalla.postman_collection.json by ensuring the npm/Python toolchain is available and running build_docs.py.
#  * @details Regenerate the API HTML page, OpenAPI JSON and Postman collection using npm assets and a Python generator.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * The CMake build calls this script so the embedded docs always match the API code.
#  * It installs missing tools into a local folder instead of changing the whole system.
#  * Without it, the shipped documentation and OpenAPI spec would go stale.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - npm: https://docs.npmjs.com/cli/
#  * - Python venv: https://docs.python.org/3/library/venv.html
#  * - Bash conditionals: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Regenerates docs/api.html, docs/openapi.json and docs/valhalla.postman_collection.json.
# Needs: node/npm (swagger-ui-dist), python3 with the "markdown" package.
#   SAMPLES_OUT=api_tests/samples.jsonl ./test_api.sh   # optional, adds real response samples
#   tools/docs/build.sh
set -euo pipefail  # e = stop on error, u = error on unset variable, pipefail = catch pipeline failures.
# Run from tools/docs so package.json, node_modules, .venv and build_docs.py all resolve locally.
cd "$(dirname "${BASH_SOURCE[0]}")"  # cd into tools/docs so package.json, node_modules and the script resolve locally.
# node_modules is gitignored, so a fresh checkout lacks swagger-ui-dist; install npm deps only when missing.
if [[ ! -d node_modules/swagger-ui-dist ]]; then  # npm is only needed on a fresh checkout where swagger-ui-dist is missing.
	# npm is only needed to fetch swagger-ui-dist, whose static assets build_docs.py inlines into api.html.
	command -v npm >/dev/null || {  # command -v checks for npm without running it.
		cat >&2 <<'MSG'
error: npm not found (Node.js is only needed to fetch swagger-ui-dist).
  macOS:          brew install node
  Debian/Ubuntu:  sudo apt install nodejs npm
  Fedora:         sudo dnf install nodejs npm
MSG
		exit 1  # Exit 1 means a required tool is missing.
	}
	npm install --no-audit --no-fund  # --no-audit/--no-fund keep npm quiet and avoid extra network calls.
fi
# Unlike npm, Python is always required because build_docs.py does the generation.
command -v python3 >/dev/null || { echo "error: python3 not found" >&2; exit 1; }  # Python is always needed because it runs the documentation generator.
PY=python3  # Remember which Python interpreter to use later.
# A missing "markdown" module triggers a local venv instead of a system-wide pip install.
if ! python3 -c 'import markdown' 2>/dev/null; then  # Try importing the markdown library to see whether it is installed.
	# Externally-managed Pythons (Homebrew, Debian 12+, Fedora) refuse pip installs: use a local venv.
	[[ -x .venv/bin/python ]] || python3 -m venv .venv  # Create a local virtual environment if one does not already exist.
	.venv/bin/python -m pip install --quiet markdown  # Install markdown into the venv instead of the system Python.
	PY=.venv/bin/python  # Switch to the venv's Python for the generation step.
fi
# Run with the selected interpreter; the generator writes all three artifacts but skips files whose content is unchanged.
"$PY" build_docs.py  # Run the generator; it rewrites only files whose content changed.
echo "Docs generated. cmake --build runs this script automatically; otherwise rebuild the server to embed them."  # Explain that the normal CMake build triggers this automatically.
