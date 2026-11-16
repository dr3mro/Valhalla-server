#!/bin/sh
# The very first line tells the computer to run this file with the plain POSIX shell (sh).

# /**
#  * @file run4bench.sh
#  * @brief Run the Release server from build/run with benchmark-tuned worker, cache and rate-limit settings.
#  * @details This script starts the Release build of the server for benchmarking.
#  * It creates and enters a clean run folder, then launches the server with settings that favour speed.
#  * The exec command hands the whole process over to the server so it receives signals directly.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the launcher used by the performance and load-testing tools.
#  * A developer or the benchmark scripts run it when they need a fast, untrottled server.
#  * Without it the built-in rate limiter would slow the load generator and the measurements would be wrong.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - POSIX sh shell basics: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
#  * - Environment variables: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */

# Resolve paths from this script and run from build/run so server-relative artifacts stay out of the repo root.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"  # Work out the repository root from where this script lives, so it works from any folder.
RUN_DIR="$ROOT/build/run"  # Folder the server will run in, so its files stay out of the repository root.
mkdir -p "$RUN_DIR"  # Create that folder if needed; -p means "do not complain if it already exists".
# Fail fast if the directory cannot be entered; the server must never start in the wrong working directory.
cd "$RUN_DIR" || exit 1  # Move into that folder and stop if that fails, so the server never starts in the wrong place.

# Hand the process to the Release server (exec forwards signals directly). The inline env vars tune it for
# benchmarking: more DB workers/threads and a larger connection pool raise throughput, the 900 s permission
# cache avoids repeated permission checks, and the very high MAX_REQUESTS/MAX_FPS keep the built-in rate
# limiter from throttling the load generator.
DB_WORKERS=5 PERMISSION_CACHE_TTL=900 SERVER_THREADS=4 DB_MAX_CONN=5 MAX_REQUESTS=10000000 MAX_FPS=3000000 exec "$ROOT/build/Release/server"  # Launch the server with the benchmark tuning described in the comment above.
