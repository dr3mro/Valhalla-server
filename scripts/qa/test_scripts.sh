#!/usr/bin/env bash
# shellcheck disable=SC2015
# Smoke/regression tests for the repository's shell scripts.
# Checks syntax, help output, and basic dispatch behaviour.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT" || exit 1

PASS=0; FAIL=0; FAILED=()
ok()   { PASS=$((PASS+1)); printf '  ok    %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); FAILED+=("$1"); printf '  FAIL  %s\n' "$1"; }
check() { # name cmd...
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then ok "$name"; else bad "$name"; fi
}
fails() { # name cmd...
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then bad "$name (expected failure)"; else ok "$name"; fi
}

section() { printf '\n== %s ==\n' "$1"; }

# 1. Syntax checks
section "syntax"
while IFS= read -r f; do
    case "$(head -n1 "$f")" in
        *bash*) bash -n "$f" && ok "bash -n $f" || bad "bash -n $f" ;;
        *) sh -n "$f" && ok "sh -n $f" || bad "sh -n $f" ;;
    esac
done < <(find scripts -name '*.sh' -type f)

# 2. Executable bits
section "executable"
for f in run.sh scripts/build/*.sh scripts/run/*.sh scripts/test/*.sh scripts/qa/*.sh; do
    [[ -x $f ]] && ok "exec $f" || bad "exec $f"
done

# 3. run.sh dispatch
section "run.sh"
check "run.sh help" ./run.sh help
check "run.sh list" ./run.sh list
fails "run.sh unknown fails" ./run.sh __no_such_command__
check "run.sh server status (no server)" ./run.sh server status

# 4. Layout constraint: only install.sh in root (plus run.sh/build helpers allowed)
section "layout"
[[ ! -f valhalla.sh ]] && ok "valhalla.sh moved" || bad "valhalla.sh still in root"
[[ -f install.sh ]] && ok "install.sh in root" || bad "install.sh missing"

# 5. qa lint
section "lint"
check "lint_sh" scripts/qa/lint_sh.sh
check "check_init_sql_path" scripts/qa/check_init_sql_path.sh

echo
echo "Passed: $PASS  Failed: $FAIL"
if (( FAIL > 0 )); then printf '%s\n' "${FAILED[@]}"; exit 1; fi
