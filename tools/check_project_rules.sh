#!/usr/bin/env bash
# Enforce project coding rules on staged C++ files (see aync_plan.md).
# Usage: tools/check_project_rules.sh            check all tracked C++ files (added lines vs HEAD)
#        tools/check_project_rules.sh --staged   check only staged C++ files
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

if [[ ${1:-} == "--staged" ]]; then
    files="$(git diff --cached --name-only --diff-filter=ACM -- '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')"
else
    files="$(git ls-files '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' '*.hxx')"
fi

status=0
while IFS= read -r f; do
    [[ -n $f ]] || continue
    [[ -f $f ]] || continue

    allow_getenv=0
    allow_cerr=0
    allow_cout=0
    case "$f" in
        src/configurator/envloader/envvars/envvars.cpp) allow_getenv=1 ;;
        src/main.cpp) allow_cerr=1 ;;
        src/server/server.cpp) allow_cout=1 ;;
    esac

    if [[ ${1:-} == "--staged" ]]; then
        diff_cmd=(git diff --cached -U0 -- "$f")
    else
        # For non-staged mode, compare working tree to HEAD.
        diff_cmd=(git diff -U0 HEAD -- "$f")
    fi

    while IFS= read -r line; do
        code="${line#+}"
        trimmed="${code#"${code%%[![:space:]]*}"}"
        [[ -z $trimmed ]] && continue
        case "$trimmed" in //*) continue ;; esac

        file_ok=1
        # stdout/stderr/printf/puts/fmt::print
        if printf '%s' "$code" | grep -qE 'std::cout|std::cerr|[[:<:]]printf[[:space:]]*\(|[[:<:]]puts[[:space:]]*\(|fmt::print'; then
            if [[ $code == *std::cout* && $allow_cout -eq 1 ]]; then
                :
            elif [[ $code == *std::cerr* && $allow_cerr -eq 1 ]]; then
                :
            else
                file_ok=0
            fi
        fi
        # getenv / setenv outside envloader
        if printf '%s' "$code" | grep -qE 'getenv|setenv'; then
            if [[ $allow_getenv -eq 0 ]]; then
                file_ok=0
            fi
        fi
        # Store:: lookups (cachedObject is allowed)
        if printf '%s' "$code" | grep -q 'Store::'; then
            if ! printf '%s' "$code" | grep -q 'Store::cachedObject'; then
                file_ok=0
            fi
        fi

        if [[ $file_ok -eq 0 ]]; then
            echo "project-rules: $f: $trimmed" >&2
            status=1
        fi
    done < <("${diff_cmd[@]}" | grep '^+' | grep -v '^+++')
done <<<"$files"

exit "$status"
