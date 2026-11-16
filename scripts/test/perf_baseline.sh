#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file perf_baseline.sh
#  * @brief Run a repeatable k6 performance sweep/benchmark and write a Markdown report plus raw metrics.
#  * @details This script runs a repeatable performance benchmark and writes a Markdown report.
#  * It can start the server, create test accounts, run several k6 load tests and sample resource use.
#  * Every option has a safe default, so running it with no arguments gives a comparable baseline.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This records the performance numbers used to judge whether code changes help or hurt.
#  * A developer or performance engineer runs it on an idle machine, often before and after a change.
#  * Without it there would be no shared numbers and speed regressions would only be noticed by chance.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - k6 load testing: https://k6.io/docs/
#  * - jq JSON processor: https://jqlang.github.io/jq/manual/
#  * - Bash trap command: https://www.gnu.org/software/bash/manual/bash.html
#  * - Docker: https://docs.docker.com/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Step 0 of PERFORMANCE_PLAN.md: record a repeatable performance baseline.
#
# What it does:
#   1. starts the server unless --no-start is given. Default: the native Release binary build/Release/server
#      (PostgreSQL must already be running and initialised, see db_init.sh; settings come from .env).
#      With --container it starts a throw-away container instead (./valhalla.sh build/run).
#   2. seeds a provider and a user (the k6 benchmark logs in as them)
#   3. runs a concurrency sweep on one read endpoint (tools/k6/sweep.js) at 1/8/32/128 VUs
#   4. runs the full mixed benchmark (tools/k6/benchmark.js: reads + crud + auth)
#   5. runs a login burst and a slow-query scenario (reads while requests wait on a locked table; needs psql)
#   5b. samples container CPU/memory while it runs
#   6. writes docs/perf/<label>.md (default: baseline.md) plus raw k6 JSON under docs/perf/raw/<label>/
#
# Usage:  tools/perf_baseline.sh [options]
#   --label NAME       output name (default: baseline). Use e.g. step3-permission-cache for later runs.
#   --container        run the server in a container (valhalla.sh) instead of the native binary
#   --reset-db         (native) re-run init.sql first: DROPS ALL DATA in the database from .env, so every run starts from the same empty tables
#   --rebuild          run "cmake --build build/Release" before starting the native binary
#   --hub              (with --container) start the published Docker Hub image instead of building this checkout (default: build)
#   --no-start         use an already running server at BASE_URL (you handle rate limits and seeding state)
#   --keep             leave the server running afterwards
#   --smoke            short run (sweep 1 and 8 VUs for 5s, benchmark in SMOKE mode) to test the script
#   --port N           host port for the container (default 8080)
#   --runtime R        (with --container) docker | container (default: container, Apple; env VALHALLA_RUNTIME also works)
#   --duration D       time per sweep level (default 20s)
#   --sweep "1 8 32"   VU levels for the sweep (default "1 8 32 128")
# Env: BASE_URL (default http://127.0.0.1:<port>), PROVIDER_USER/PROVIDER_PASS, USER_NAME/USER_PASS,
#      WHITELIST (IP patterns for the rate limiter, default "127.0.0.1,172.*,192.168.*,10.*")
# Needs: k6, curl, jq (and the container runtime unless --no-start).
# Note: the rate limiter / ban list will return 429/403 under load; if you see them in the results,
#       widen WHITELIST (see the RL_* / BAN_* / WHITELIST notes in valhalla.sh).
# No `-e`: k6 threshold failures and optional scenarios are non-fatal; each stage logs its own outcome.
set -uo pipefail  # Treat unset variables and failed pipeline steps as errors.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # Absolute repository root, found from this file's own location.
# Defaults favour a safe, repeatable run; every knob can be overridden by a flag or an env var.
MODE=native; REBUILD=0; RESET_DB=0; ENV_FILE="${ENV_FILE:-}"  # Defaults: use the native binary, do not rebuild, do not reset the database.
LABEL="baseline"; START=1; BUILD=1; KEEP=0; SMOKE=0; PORT=8080; RUNTIME="${RUNTIME:-${VALHALLA_RUNTIME:-container}}"  # Output name, start/build flags, port and container runtime defaults.
DURATION="20s"; SWEEP="1 8 32 128"  # How long each sweep step runs and the VU levels to test.
# $$ makes the throwaway container name unique so parallel runs cannot collide.
NAME="perf-$$"  # Unique throwaway container name based on this script's process id.
while [[ $# -gt 0 ]]; do  # Read each command-line option in turn.
	case "$1" in  # Work out which option this is.
		--label) LABEL="$2"; shift 2 ;;  # Choose a name for this run's report and raw files.
		--no-start) START=0; shift ;;  # Use an already running server instead of launching one.
		--hub) BUILD=0; shift ;;  # Run the published image instead of building this checkout.
		--container) MODE=container; shift ;;  # Run the server inside a throwaway container.
		--native) MODE=native; shift ;;  # Run the native Release binary directly.
		--rebuild) REBUILD=1; shift ;;  # Rebuild the native binary before starting it.
		--reset-db) RESET_DB=1; shift ;;  # Recreate the database tables before this run.
		--env-file) ENV_FILE="$2"; shift 2 ;;  # Read server settings from a specific env file.
		--keep) KEEP=1; shift ;;  # Leave the server running when the benchmark finishes.
		--smoke) SMOKE=1; DURATION="5s"; SWEEP="1 8"; shift ;;  # Short test run with fewer levels so the script can be checked quickly.
		--port) PORT="$2"; shift 2 ;;  # Host port to expose for the container or native server.
		--runtime) RUNTIME="$2"; shift 2 ;;  # Choose which container runtime to use.
		--duration) DURATION="$2"; shift 2 ;;  # Set the length of each sweep level.
		--sweep) SWEEP="$2"; shift 2 ;;  # Set the list of VU levels for the sweep.
		# NOTE: --help prints a hard-coded slice of this file's own lines (lines 2 to 28).
		# Adding comment lines near the top shifts which lines that slice shows; the range is left unchanged on purpose.
		-h|--help) sed -n '2,28p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;  # Reject any option we do not recognise.
	esac
done

# Reset must happen before this script starts the server; with --no-start there is no safe point to do it.
if [[ ${RESET_DB:-0} == 1 && $START == 0 ]]; then  # Resetting the database is unsafe when we did not start the server ourselves.
	echo "error: --reset-db only works when this script starts the server (it resets the database first, then starts it)." >&2  # Explain the problem to the caller.
	echo "       With --no-start, reset by hand, then start your server, then run the script:" >&2  # Give the safe sequence of steps instead.
	echo "         set -a; . ./.env; set +a; PGPASSWORD=\"\$DB_PASS\" psql -q -h \"\$DB_HOST\" -p \"\${DB_PORT:-5432}\" -U \"\$DB_USER\" -d \"\$DB_NAME\" -f init.sql" >&2
	exit 2  # Stop before touching anything.
fi

BASE_URL="${BASE_URL:-http://127.0.0.1:$PORT}"  # Address the load generator will call.
# These passwords are fixed fixtures for local throwaway accounts, not real credentials.
PROVIDER_USER="${PROVIDER_USER:-provider}"; PROVIDER_PASS="${PROVIDER_PASS:-1234@Abc!}"  # Fixture account the k6 scripts log in as; not real credentials.
USER_NAME="${USER_NAME:-user1}"; USER_PASS="${USER_PASS:-123Mm@p0}"  # Second fixture account used by the scenarios.
# Whitelisting local/private ranges keeps the load generator from being 429/403-banned mid-run.
WHITELIST="${WHITELIST:-127.0.0.1,172.*,192.168.*,10.*}"  # Local and private addresses that the rate limiter must not ban.

# Fail before starting anything if a required benchmark tool is missing.
for t in k6 curl jq; do command -v "$t" >/dev/null || { echo "error: $t is required (macOS: brew install $t)" >&2; exit 2; }; done  # Fail before doing any work if a required benchmark tool is missing.

# Keep raw JSON/logs per label so separate runs can be compared side by side.
OUT_DIR="$ROOT/docs/perf"; RAW="$OUT_DIR/raw/$LABEL"; mkdir -p "$RAW"  # Folder for this run's report and raw metrics.
REPORT="$OUT_DIR/$LABEL.md"  # Where the final Markdown report will be written.
# Private temp dir holds the generated container env file; PIDs are tracked so cleanup can stop them.
TMP="$(mktemp -d)"; STATS_PID=""; SRV_PID=""  # Private temp folder and empty slots for the two background process ids.
# Pre-build the valhalla.sh invocation shared by all container commands.
V=("$ROOT/valhalla.sh")  # Start building the command array that drives a container run.
[[ -n $RUNTIME ]] && V+=(--runtime "$RUNTIME")  # Add the runtime flag when one was chosen.
V+=(--name "$NAME" --port "$PORT")  # Give the throwaway container a unique name and port.

# @brief Tear down everything this run created: sampler, native server, throwaway container and temp files.
# @note Honours KEEP (leave server/container running) and START (nothing was launched with --no-start).
# @note Container delete tries -y first, then falls back to the interactive form; stderr is suppressed.
cleanup() {
	[[ -n $STATS_PID ]] && kill "$STATS_PID" 2>/dev/null  # Stop the resource sampler if it is running.
	if [[ -n ${SRV_PID:-} && $KEEP == 0 ]]; then kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; fi  # Stop the native server unless --keep was requested.
	if [[ $MODE == container && $START == 1 && $KEEP == 0 ]]; then "${V[@]}" delete -y >/dev/null 2>&1 || "${V[@]}" delete >/dev/null 2>&1 </dev/null; fi  # Remove the throwaway container unless --keep was requested.
	rm -rf "$TMP"  # Delete the private temp folder.
}
# Runs on every exit path (including errors and interrupts) so a failed run never leaks a server or container.
trap cleanup EXIT  # Always run cleanup, including on errors and interrupts.

# @brief Print a section banner to make the long run's phases easy to follow in the console log.
# @param $* Section title.
log() { printf '\n== %s ==\n' "$*"; }

# ---- 1. server
SERVER_LOG="$RAW/server.log"  # File that captures the server's own output.
# Native mode runs the already-built Release binary unless --rebuild was requested.
if [[ $START == 1 && $MODE == native ]]; then  # Launch the native binary branch.
	BIN="$ROOT/build/Release/server"  # The Release binary we will run.
	if [[ $REBUILD == 1 ]]; then  # Rebuild first only when asked.
		log "Building (cmake --build build/Release)"  # Announce the build step.
		cmake --build "$ROOT/build/Release" || { echo "error: build failed" >&2; exit 1; }  # Build the binary and stop if that fails.
	fi
	[[ -x $BIN ]] || { echo "error: $BIN not found; run ./build.sh or use --rebuild" >&2; exit 1; }  # Refuse to start if the binary is missing or not executable.
	log "Starting native server $BIN on port $PORT"  # Announce the native launch.
	# Load KEY=VALUE lines from the env file (never sourced, same parser idea as run_release.sh).
	if [[ -z ${ENV_FILE:-} ]]; then  # Pick an env file unless the caller chose one.
		if [[ -f $ROOT/config ]]; then EF="$ROOT/config"  # Prefer the config file.
		elif [[ -f $ROOT/.env ]]; then EF="$ROOT/.env"  # Otherwise use .env.
		else EF="$ROOT/config"  # Otherwise point at config so we can note it is missing.
		fi
	else
		EF="$ENV_FILE"  # Use the specific env file the caller supplied.
	fi
	if [[ -f $EF ]]; then  # Only read settings if the file exists.
		while IFS= read -r line || [[ -n $line ]]; do  # Read every line, including a final one with no newline.
			line=${line%$'\r'}  # Remove a Windows-style carriage return.
			# Skip blank lines and comment lines in the env file.
			[[ $line =~ ^[[:space:]]*(#|$) ]] && continue
			[[ $line =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue  # Keep only KEY=VALUE lines, optionally starting with export.
			key=${BASH_REMATCH[2]}; val=${BASH_REMATCH[3]}  # Capture the name and value from that pattern.
			val=${val%%[[:space:]]#*}; val=${val%"${val##*[![:space:]]}"}  # Drop any trailing comment and surrounding spaces from the value.
			if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then val=${BASH_REMATCH[1]}; fi  # Strip matching quotes from the value if present.
			export "$key=$val"  # Export the pair so the server inherits it.
		done <"$EF"  # Feed the env file into the loop.
	else
		echo "note: no env file at $EF, using the environment and built-in defaults"  # Explain that we are falling back to existing settings.
	fi
	if [[ $RESET_DB == 1 ]]; then  # Recreate the tables only when --reset-db was given.
		command -v psql >/dev/null || { echo "error: --reset-db needs psql" >&2; exit 1; }  # Check that the database command exists first.
		log "Resetting database ${DB_NAME:-postgres} on ${DB_HOST:-127.0.0.1}:${DB_PORT:-5432} (init.sql, drops all data)"  # Warn loudly that all data will be dropped.
		PGPASSWORD="${DB_PASS:-}" psql -q -v ON_ERROR_STOP=1 -h "${DB_HOST:-127.0.0.1}" -p "${DB_PORT:-5432}" -U "${DB_USER:-postgres}" -d "${DB_NAME:-postgres}" -f "$ROOT/scripts/db/init.sql" >/dev/null \
			|| { echo "error: init.sql failed" >&2; exit 1; }  # Stop if the database reset fails.
	fi
	# Perf-run overrides: our port, no rate-limit bans, quiet console (the log goes to $SERVER_LOG).
	export SERVER_PORT="$PORT" MAX_REQUESTS=100000000 MAX_FPS=100000000 WHITELIST="$WHITELIST" SERVER_LOG_TO_CONSOLE=false  # Disable rate limiting and console noise for a fair benchmark.
	# Refuse to start on an occupied port instead of silently benchmarking some other process.
	if lsof -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then echo "error: port $PORT is already in use; stop that server first" >&2; exit 1; fi  # Refuse to benchmark some other process already on this port.
	mkdir -p "$ROOT/build/run"  # Make sure the run folder exists.
	# Launch in the background from build/run; save the PID for readiness checks and cleanup.
	( cd "$ROOT/build/run" && exec "$BIN" ) >"$SERVER_LOG" 2>&1 &  # Start the server in the background from the run folder, logging its output.
	SRV_PID=$!  # Remember its process id for health checks and cleanup.
elif [[ $START == 1 ]]; then  # Otherwise start a throwaway container.
	log "Starting server ($NAME on port $PORT)"  # Announce the container launch.
	# Feed the same rate-limit overrides through valhalla.sh so container runs are comparable to native ones.
	printf 'WHITELIST=%s\nMAX_REQUESTS=100000000\nMAX_FPS=100000000\n' "$WHITELIST" >"$TMP/env"  # Create a small env file with the same rate-limit overrides.
	# --hub selects the published image; otherwise build this checkout, tagging the image with the label.
	if [[ $BUILD == 1 ]]; then CMD=(build "$LABEL"); echo "Building the image from this checkout (first build can take several minutes)"; else CMD=(run); fi  # Build this checkout by default, or run the published image with --hub.
	"${V[@]}" --env-file "$TMP/env" "${CMD[@]}" || { echo "error: could not start the server" >&2; exit 1; }  # Run the container command and stop if it fails.
fi
# Poll the health endpoint for up to ~90 s; a dead native server is reported immediately with its log tail.
for _ in $(seq 1 90); do  # Poll for up to about 90 seconds while the server starts.
	curl -fsS --max-time 3 "$BASE_URL/api/v2/hello" >/dev/null 2>&1 && break  # Stop waiting as soon as the health endpoint answers.
	if [[ -n $SRV_PID ]] && ! kill -0 "$SRV_PID" 2>/dev/null; then  # If the native server died, report it immediately.
		echo "error: the server exited. Last log lines ($SERVER_LOG):" >&2; tail -20 "$SERVER_LOG" >&2  # Show the tail of the log to help diagnose the crash.
		echo "hint: PostgreSQL must be running and initialised (psql -f init.sql via ./db_init.sh), matching DB_* in .env" >&2  # Suggest the most common cause.
		exit 1
	fi
	sleep 1  # Wait one second before the next health probe.
done  # Continue until every VU level has been tested.
curl -fsS --max-time 3 "$BASE_URL/api/v2/hello" >/dev/null 2>&1 || { echo "error: server not answering at $BASE_URL" >&2; exit 1; }  # Final check: give up if the server never became healthy.

# ---- 2. seed provider + user (ignore "already exists")
log "Seeding"  # Announce account creation.
# @brief POST a JSON body and print only the HTTP status, so callers can ignore "already exists" replies.
# @param $1 API path relative to /api/v2; $2 JSON request body.
post() { curl -sS --max-time 15 -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -X POST "$BASE_URL/api/v2$1" -d "$2"; }  # Small helper that returns only the HTTP status of a POST call.
PROVIDER_JSON="$(jq -nc --arg u "$PROVIDER_USER" --arg p "$PROVIDER_PASS" '{avatar:"http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",active:true,language:"en",username:$u,password:$p,fullname:"Amr Nasr",phone:"+201299999999",email:"amr@mail.com",country:"Egypt",city:"Damietta",address:"portsaid street",dob:"17-02-1985",gender:"Male",position:"Doctor",speciality:"Cardiology"}')"
USER_JSON="$(jq -nc --arg u "$USER_NAME" --arg p "$USER_PASS" '{avatar:"http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",language:"en",active:true,parent:"0",username:$u,email:"amr@mail.com",password:$p,fullname:"Amr Nasr",phone:"+201299999999",country:"Egypt",city:"Damietta",address:"12 valhalla Street ",dob:"10-10-1990",gender:"Male",marital:"single",job:"Doctor"}')"
echo "provider create: $(post /clients/providers/create "$PROVIDER_JSON")   user create: $(post /clients/users/create "$USER_JSON")"  # Create both fixture accounts; an 'already exists' reply is fine.
LOGIN="$(curl -sS --max-time 15 -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -X POST "$BASE_URL/api/v2/clients/providers/login" -d "$(jq -nc --arg u "$PROVIDER_USER" --arg p "$PROVIDER_PASS" '{username:$u,password:$p}')")"
# The k6 scripts log in as this provider, so a failed login means every benchmark would measure errors.
[[ $LOGIN == 200 ]] || { echo "error: provider login returned $LOGIN (check seeding / rate limiter)" >&2; exit 1; }  # A failed login would make every benchmark measure errors.

# The k6 JavaScript scripts consume these seeded identities as environment variables.
export BASE_URL PROVIDER_USER PROVIDER_PASS USER_NAME USER_PASS  # Pass the seeded identities to the k6 scripts.

if [[ $START == 0 ]]; then  # Warn when reusing a server we did not configure.
	echo "NOTE: --no-start needs a server started with MAX_REQUESTS=100000000, MAX_FPS=100000000 and a WHITELIST,"  # Tell the caller which settings the external server needs.
	echo "      otherwise the rate limiter (default 10000 requests/30 s per IP) bans the load generator for an hour."
fi

# ---- 3. CPU/memory sampler (one snapshot every 5 s, never a streaming call)
if [[ -n $SRV_PID ]]; then  # Sample a native server through the operating system.
	( while kill -0 "$SRV_PID" 2>/dev/null; do  # Keep sampling while the native server is alive.
		ps -o %cpu=,rss= -p "$SRV_PID" | awk -v t="$(date +%H:%M:%S)" '{printf "%s %s%% %.0fMiB\n", t, $1, $2/1024}'  # Take one CPU and memory snapshot and format it with a timestamp.
		sleep 5
	done ) >"$RAW/container-stats.txt" &  # Save all samples to a file in the background.
	STATS_PID=$!  # Remember the native sampler's process id so cleanup can stop it.
# Containers are sampled through the runtime only when docker is available (Apple containers lack `stats`).
elif [[ $MODE == container && $START == 1 && ( $RUNTIME == docker || ( -z $RUNTIME && -n $(command -v docker) ) ) ]]; then  # Sample a container only when the runtime supports the stats command.
	RT="${RUNTIME:-docker}"  # The runtime command used for sampling.
	( while :; do  # Sample the container forever until cleanup kills this loop.
		"$RT" stats --no-stream --format '{{.CPUPerc}} {{.MemUsage}}' "$NAME" 2>/dev/null | sed "s/^/$(date +%H:%M:%S) /"  # Take one non-streaming stats snapshot and timestamp it.
		sleep 5
	done ) >"$RAW/container-stats.txt" &  # Save container samples in the background too.
	STATS_PID=$!  # Remember the container sampler's process id.
fi

# ---- 4. concurrency sweep
log "Concurrency sweep (providers/read): $SWEEP, $DURATION each"  # Announce the sweep phase.
for n in $SWEEP; do  # Run the load test once for each VU level.
	echo "-- $n VUs for $DURATION ($(date +%H:%M:%S))"  # Log which level is running and when.
	# A non-zero k6 exit (e.g. a failed threshold) is logged, not fatal: the raw metrics still go into the report.
	k6 run --quiet -e VUS="$n" -e DURATION="$DURATION" --summary-export "$RAW/sweep-$n.json" "$ROOT/tools/k6/sweep.js" >"$RAW/sweep-$n.log" 2>&1 \
		|| echo "   (k6 exited non-zero; see $RAW/sweep-$n.log)"  # A failed threshold is logged, not fatal; raw metrics are still saved.
done  # Continue until every summary has been checked.

# ---- 5. full mixed benchmark
log "Mixed benchmark (reads + crud + auth), takes about 2 minutes, live log: $RAW/benchmark.log"  # Announce the longer mixed workload.
if [[ $SMOKE == 1 ]]; then export SMOKE=1; fi  # Tell k6 scripts to shorten their own run in smoke mode.
k6 run --quiet --summary-export "$RAW/benchmark.json" "$ROOT/tools/k6/benchmark.js" >"$RAW/benchmark.log" 2>&1 \
	|| echo "   (k6 exited non-zero, usually a threshold; see $RAW/benchmark.log)"  # Log a threshold failure without aborting the report.

# ---- 5b. login burst: do many simultaneous logins slow down plain reads?
log "Login burst (reads alone, then reads while clients log in), about 40 seconds"  # Announce the login-burst scenario.
# the mixed benchmark may have removed the provider account, so make sure it exists again (create is a no-op if it does)
post /clients/providers/create "$PROVIDER_JSON" >/dev/null || true  # Re-create the provider in case the mixed benchmark deleted it.
k6 run --quiet --summary-export "$RAW/login-burst.json" "$ROOT/tools/k6/login_burst.js" >"$RAW/login-burst.log" 2>&1 \
	|| echo "   (k6 exited non-zero; see $RAW/login-burst.log)"  # Log a non-zero k6 result without aborting.

# ---- 5c. slow query: does a query stuck in the database slow down unrelated reads?
# A background psql locks the users table during the second phase, so reads of that table wait inside the database while
# reads of the providers table go on. Needs psql and the same DB_* settings the server uses (native mode loads them from .env).
SLOW_DONE=0  # Assume the slow-query scenario will not complete.
if command -v psql >/dev/null; then  # Only try the slow query when psql is available.
	SLOW_PHASE=15  # Number of seconds the second phase lasts.
	log "Slow query (reads alone, then reads while ${SLOW_VUS:-4} clients wait on a locked table), about 45 seconds"  # Announce the database-locking scenario.
	# the mixed benchmark removes the accounts it logs in as, so create both again (a no-op if they exist)
	post /clients/providers/create "$PROVIDER_JSON" >/dev/null || true  # Re-create the provider account before the slow-query scenario.
	post /clients/users/create "$USER_JSON" >/dev/null || true  # Re-create the user account before the slow-query scenario.
	# Background psql session: lock the users table during the second slow-query phase.

	(
		sleep $((SLOW_PHASE + 5 + 1))  # Let the first k6 phase finish before taking the table lock.
		PGPASSWORD="${DB_PASS:-}" psql -q -h "${DB_HOST:-127.0.0.1}" -p "${DB_PORT:-5432}" -U "${DB_USER:-postgres}" -d "${DB_NAME:-postgres}" \
			-c "BEGIN; LOCK TABLE users IN ACCESS EXCLUSIVE MODE; SELECT pg_sleep($((SLOW_PHASE - 1))); COMMIT;" >"$RAW/slow-query-lock.log" 2>&1  # Lock the table for a while so reads must wait, then release it.
	) &  # Run the locker in the background while k6 runs.
	LOCK_PID=$!  # Remember the locker's process id.
	k6 run --quiet -e PHASE="${SLOW_PHASE}s" --summary-export "$RAW/slow-query.json" "$ROOT/tools/k6/slow_query.js" >"$RAW/slow-query.log" 2>&1 \
		|| echo "   (k6 exited non-zero; see $RAW/slow-query.log)"  # Log a non-zero k6 result without aborting.
	# Wait for the background locker to commit and release the table lock before moving on.
	wait "$LOCK_PID" 2>/dev/null  # Wait for the lock to be released before moving on.
	SLOW_DONE=1  # Mark the slow-query scenario as completed.
	if grep -qi "error\|fatal" "$RAW/slow-query-lock.log" 2>/dev/null; then echo "WARNING: the table lock failed, see $RAW/slow-query-lock.log"; SLOW_DONE=0; fi  # If the lock failed, mark the scenario as skipped.
else
	echo "note: psql not found, skipping the slow-query scenario"  # Explain why this optional scenario was skipped.
fi

[[ -n $STATS_PID ]] && { kill "$STATS_PID" 2>/dev/null; STATS_PID=""; }  # Stop the sampler now that load generation is over.

# A run where most requests failed measures the rate limiter, not the server: refuse to call it a result.
BAD=0  # Flag that marks a run with too many failed requests.
for f in "$RAW"/sweep-*.json "$RAW/benchmark.json"; do  # Inspect each k6 summary for a high failure rate.
	[[ -s $f ]] || continue  # Skip files that are missing or empty.
	r="$(jq -r '(.metrics.http_req_failed.value // .metrics.http_req_failed.rate // 0)' "$f" 2>/dev/null)"  # Read the failed-request fraction from the k6 summary.
	if awk -v r="$r" 'BEGIN{exit !(r > 0.05)}'; then echo "WARNING: $(basename "$f") failed rate $r (rate limiter ban / 429 / 503?)"; BAD=1; fi  # More than five percent failures means the numbers are not trustworthy.
done

# ---- 6. report
log "Writing $REPORT"  # Announce the report-writing phase.
# @brief Extract one jq-computed value from a k6 summary, substituting "n/a" when it is missing.
# @param $1 Summary JSON file; $2 key label; $3 jq expression.
m() { jq -r --arg k "$2" "$3" "$1" 2>/dev/null || echo "n/a"; }   # m <file> <key> <jq>
# @brief Emit one Markdown table row for a login-burst or slow-query scenario.
# @param $1 Row label; $2 k6 scenario name for reads; $3 competing-load scenario (empty for none).
# @param $4 Optional summary JSON file, defaulting to the login-burst summary.
burst_row() { # burst_row <label> <read scenario> <other scenario or empty> [json file]
	local f="${4:-$RAW/login-burst.json}"  # Summary file to read; defaults to the login-burst results.
	[[ -s $f ]] || { echo "| $1 | n/a | n/a | n/a | n/a |"; return; }  # A missing file yields a row of placeholders.
	jq -r --arg l "$1" --arg rs "$2" --arg ls "$3" '
		.metrics as $m |
		($m["http_req_duration{scenario:" + $rs + "}"] // {}) as $d |
		($m["http_reqs{scenario:" + $rs + "}"] // {}) as $q |
		(if $ls == "" then "-" else ($m["http_reqs{scenario:" + $ls + "}"].rate // 0 | . * 10 | round / 10 | tostring) end) as $lr |
		"| \($l) | \($q.rate // 0 | . * 10 | round / 10) | \($d["p(95)"] // 0 | . * 10 | round / 10) | \($d["p(99)"] // 0 | . * 10 | round / 10) | \($lr) |"
	' "$f"
}
# @brief Emit one Markdown row of throughput, latency percentiles and failure rate from a k6 summary.
# @param $1 Row label; $2 summary JSON file; a missing/empty file yields a row of "n/a" cells.
row() { # row <label> <json>
	local f="$2"  # The summary file passed to the row helper.
	[[ -s $f ]] || { echo "| $1 | n/a | n/a | n/a | n/a | n/a | n/a |"; return; }
	jq -r --arg l "$1" '
		.metrics as $m |
		"| \($l) | \($m.http_reqs.rate // 0 | . * 10 | round / 10) | \($m.http_req_duration.med // 0 | . * 10 | round / 10) | \($m.http_req_duration["p(95)"] // 0 | . * 10 | round / 10) | \($m.http_req_duration["p(99)"] // 0 | . * 10 | round / 10) | \((($m.http_req_failed.value // $m.http_req_failed.rate // 0) * 10000 | round) / 100)% | \($m.http_reqs.count // 0) |"
	' "$f"
}
# Build the whole Markdown report in one redirected block so every section reads the same set of results.
{
	echo "# Performance run: $LABEL"
	echo
	echo "- Date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "- Git: $(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo unknown)"
	echo "- Host: $(uname -srm), $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null) CPUs"
	if [[ $START == 0 ]]; then
		target_desc="existing server"
	elif [[ $MODE == native ]]; then
		target_desc="native build/Release/server, threads=${SERVER_THREADS:-default}, db_conn=${DB_MAX_CONN:-default}, db_workers=${DB_WORKERS:-default}"
	else
		target_desc="fresh ${RUNTIME} container"
	fi
	echo "- Target: $BASE_URL ($target_desc)"
	echo "- Sweep: ${DURATION} per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)"
	[[ $SMOKE == 1 ]] && echo "- **Smoke run: numbers are not meaningful**"
	[[ $BAD == 1 ]] && echo "- **INVALID RUN: more than 5% of requests failed (see warnings). Fix the cause and rerun; do not compare these numbers.**"
	echo
	echo "## Concurrency sweep"
	echo
	echo "| VUs | req/s | median ms | p95 ms | p99 ms | failed | requests |"
	echo "|-----|-------|-----------|--------|--------|--------|----------|"
	for n in $SWEEP; do row "$n" "$RAW/sweep-$n.json"; done
	echo
	echo "## Mixed benchmark (reads + crud + auth)"
	echo
	echo "| scenario | req/s | median ms | p95 ms | p99 ms | failed | requests |"
	echo "|----------|-------|-----------|--------|--------|--------|----------|"
	row "all" "$RAW/benchmark.json"
	echo
	echo "Per-scenario p95 and thresholds: \`raw/$LABEL/benchmark.log\`."
	echo
	echo "## Login burst (read latency with and without simultaneous logins)"
	echo
	echo "| phase | read req/s | read p95 ms | read p99 ms | login req/s |"
	echo "|-------|-----------|-------------|-------------|-------------|"
	burst_row "reads alone" "reads_quiet" ""
	burst_row "reads during login burst" "reads_burst" "logins"
	echo
	echo "## Slow query (read latency while other requests wait on a locked table)"
	echo
	if [[ $SLOW_DONE == 1 ]]; then
		echo "| phase | read req/s | read p95 ms | read p99 ms | slow req/s |"
		echo "|-------|-----------|-------------|-------------|-----------|"
		burst_row "reads alone" "reads_quiet" "" "$RAW/slow-query.json"
		burst_row "reads while ${SLOW_VUS:-4} requests are stuck" "reads_slow" "slow_users" "$RAW/slow-query.json"
		echo
		echo "Read p99 should stay close to the first row. If it follows the stuck requests, database work is still blocking request threads (or SLOW_VUS is not below DB_WORKERS and SERVER_THREADS)."
	else
		echo "Skipped (no psql, or the table lock failed)."
	fi
	echo
	if [[ -s $RAW/container-stats.txt ]]; then
		echo "## Container CPU / memory (peak sample)"
		echo
		echo '```'
		sort -t' ' -k2 -r "$RAW/container-stats.txt" | head -3
		echo '```'
		echo
	fi
	echo "## Checks before trusting the numbers"
	echo
	echo "- A non-zero *failed* column usually means 429/403 from the rate limiter or connection-pool timeouts, not slowness. Look at \`raw/$LABEL/*.log\` and the server log."
	echo "- Run on an idle machine, with the load generator and the server not competing for the same cores if possible."
	echo "- Compare only runs with the same duration, sweep levels and hardware."
} >"$REPORT"  # Send the whole report block into the Markdown file.

# Point the caller at both the summary and the raw artifacts for later comparison.
echo "Done. Report: $REPORT"  # Tell the caller where the summary is.
echo "Raw k6 output: $RAW"  # And where the raw metrics are for later comparison.
