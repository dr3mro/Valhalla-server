#!/usr/bin/env bash
# Lint shell scripts exactly like CI (.github/workflows/scripts.yml): bash scripts with
# `shellcheck -s bash`, everything else with `shellcheck -s sh` plus `dash -n`.
# Usage: tools/lint_sh.sh            lint every tracked *.sh
#        tools/lint_sh.sh --staged   lint only the staged *.sh files (used by .githooks/pre-commit)
# Needs: shellcheck and dash (macOS: brew install shellcheck dash).
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

for tool in shellcheck dash; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool not found (macOS: brew install shellcheck dash)" >&2
        exit 2
    }
done

if [[ ${1:-} == "--staged" ]]; then
    files="$(git diff --cached --name-only --diff-filter=ACM -- '*.sh')"
else
    files="$(git ls-files '*.sh')"
fi

status=0
while IFS= read -r f; do
    [[ -n $f ]] || continue
    case "$(head -n 1 "$f")" in
        *bash*) shellcheck -s bash "$f" || status=1 ;;
        *) shellcheck -s sh "$f" && dash -n "$f" || status=1 ;;
    esac
done <<<"$files"
exit "$status"
