#!/usr/bin/env bash
# /**
#  * @file check_init_sql_path.sh
#  * @brief Detect bare "init.sql" references that broke when the schema moved to scripts/db, while allowing documented, container, and variable-based paths.
#  * @details Find bare 'init.sql' references that stopped working when the schema moved to scripts/db, while allowing documented and container paths.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * The database schema was moved in commit c79b555; old paths fail silently at runtime.
#  * This check runs in CI and pre-commit to catch those references before they ship.
#  * Without it, a typo in a path would only show up when someone starts the server.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - grep: https://www.gnu.org/software/grep/manual/
#  * - git diff: https://git-scm.com/docs/git-diff
#  * - Bash parameter expansion: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Guard against a bare "init.sql" path reference after the schema file moved
# to scripts/db (commit c79b555). A reference like `"init.sql"` or `-f init.sql`
# with no directory prefix silently breaks, because the file no longer lives at
# the project root.
# Usage: scripts/qa/check_init_sql_path.sh            check all tracked files
#        scripts/qa/check_init_sql_path.sh --staged   check only staged files
# -u and pipefail catch typos and hidden pipeline errors; -e is omitted because status is accumulated across all hits.
set -uo pipefail  # u and pipefail catch mistakes; no -e so every bad reference can be reported.

# Run from the repository root so git's file list and paths resolve consistently.
cd "$(git rev-parse --show-toplevel)" || exit 1  # Run from the repository root so listed paths match files on disk.

# @brief Return 0 for files where "init.sql" appears only in prose, generated output, container paths, or is the schema itself.
# @param $1 repository-relative file path to test.
# @return 0 to skip the file from scanning, 1 to scan it.
# Files where init.sql is only mentioned in prose / generated output / container paths.
skip_file() {  # Returns 0 = skip this file, 1 = scan it.
    case "$1" in
        README.md) return 0 ;;  # README only talks about the filename in prose.
        docs/*) return 0 ;;  # The docs folder contains documentation, not runtime code.
        tools/docs/guide.md) return 0 ;;  # The generated guide only mentions the name as text.
        .github/*) return 0 ;;  # CI files have their own container paths.
        scripts/test/perf_baseline.sh) return 0 ;;  # The perf script prints the name as descriptive text.
        scripts/vdt/valhalla.sh) return 0 ;;  # The vdt script uses container-side paths for this file.
        docker/entrypoint.sh) return 0 ;;  # The container entrypoint legitimately uses /app/init.sql.
        tools/docs/build_docs.py) return 0 ;;  # The docs generator only writes prose about the file.
        scripts/db/init.sql) return 0 ;;  # The schema file itself is naturally allowed to mention its own name.
        *) return 1 ;;  # Any file not listed above is scanned.
    esac
}

if [[ ${1:-} == "--staged" ]]; then  # --staged limits the check to files about to be committed.
    # --diff-filter=ACM keeps added/copied/modified files, the only ones that can introduce bad references.
    files="$(git diff --cached --name-only --diff-filter=ACM)"  # Only added, copied or modified files can introduce a bad reference.
else
    files="$(git ls-files)"  # Without --staged, check every tracked file.
fi

# Accumulate status so every offending reference is reported before exiting.
status=0  # Start clean; set to 1 when a bad path is found.
while IFS= read -r f; do  # Read one filename from the list at a time.
    [[ -n $f ]] || continue
    [[ -f $f ]] || continue
    # Short-circuit: exempt files are not scanned at all.
    skip_file "$f" && continue  # Skip exempt files without even scanning them.

    while IFS= read -r hit; do  # Read each grep hit, which is one matching line.
        [[ -z $hit ]] && continue
        # grep -n output is "file:line:text"; split off the text part.
        text="${hit#*:*:}"  # Strip 'file:line:' and keep only the source text.
        # Bash idiom: strip leading whitespace so comment lines can be recognized below.
        trimmed="${text#"${text%%[![:space:]]*}"}"  # Remove leading whitespace so a leading # can be recognized.
        # Skip comment lines.
        # A mention in prose/comment is not a runtime path reference.
        case "$trimmed" in \#*) continue ;; esac  # A comment only mentions the name; it is not a real path use.
        # A reference is acceptable if it points at the moved file or the
        # container-side copy, or uses a variable expansion.
        # The variable-expansion alternative avoids false positives for paths assembled at runtime.
        if ! printf '%s' "$text" | grep -qE 'scripts/db/init\.sql|/app/init\.sql|\$[A-Za-z_][A-Za-z0-9_]*[^ ]*init\.sql'; then  # Accept the moved path, the container copy, or a variable-built path.
            echo "init-sql-path: $hit" >&2
            status=1
        fi
    done < <(grep -n 'init\.sql' "$f" 2>/dev/null)  # grep -n gives 'file:line:text'; the <(...) form feeds the loop.
done <<<"$files"  # Feed the file list into the loop.

exit "$status"  # 0 = no bad references; 1 = at least one was found.
