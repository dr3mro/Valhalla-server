#!/usr/bin/env bash
# Guard against a bare "init.sql" path reference after the schema file moved
# to scripts/db (commit c79b555). A reference like `"init.sql"` or `-f init.sql`
# with no directory prefix silently breaks, because the file no longer lives at
# the project root.
# Usage: scripts/qa/check_init_sql_path.sh            check all tracked files
#        scripts/qa/check_init_sql_path.sh --staged   check only staged files
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

# Files where init.sql is only mentioned in prose / generated output / container paths.
skip_file() {
    case "$1" in
        README.md) return 0 ;;
        docs/*) return 0 ;;
        tools/docs/guide.md) return 0 ;;
        .github/*) return 0 ;;
        scripts/test/perf_baseline.sh) return 0 ;;
        scripts/vdt/valhalla.sh) return 0 ;;
        docker/entrypoint.sh) return 0 ;;
        tools/docs/build_docs.py) return 0 ;;
        scripts/db/init.sql) return 0 ;;
        *) return 1 ;;
    esac
}

if [[ ${1:-} == "--staged" ]]; then
    files="$(git diff --cached --name-only --diff-filter=ACM)"
else
    files="$(git ls-files)"
fi

status=0
while IFS= read -r f; do
    [[ -n $f ]] || continue
    [[ -f $f ]] || continue
    skip_file "$f" && continue

    while IFS= read -r hit; do
        [[ -z $hit ]] && continue
        # grep -n output is "file:line:text"; split off the text part.
        text="${hit#*:*:}"
        trimmed="${text#"${text%%[![:space:]]*}"}"
        # Skip comment lines.
        case "$trimmed" in \#*) continue ;; esac
        # A reference is acceptable if it points at the moved file or the
        # container-side copy, or uses a variable expansion.
        if ! printf '%s' "$text" | grep -qE 'scripts/db/init\.sql|/app/init\.sql|\$[A-Za-z_][A-Za-z0-9_]*[^ ]*init\.sql'; then
            echo "init-sql-path: $hit" >&2
            status=1
        fi
    done < <(grep -n 'init\.sql' "$f" 2>/dev/null)
done <<<"$files"

exit "$status"
