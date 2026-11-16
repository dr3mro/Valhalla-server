#!/usr/bin/env bash
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
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE=native; REBUILD=0; RESET_DB=0; ENV_FILE="${ENV_FILE:-}"
LABEL="baseline"; START=1; BUILD=1; KEEP=0; SMOKE=0; PORT=8080; RUNTIME="${RUNTIME:-${VALHALLA_RUNTIME:-container}}"
DURATION="20s"; SWEEP="1 8 32 128"
NAME="perf-$$"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--label) LABEL="$2"; shift 2 ;;
		--no-start) START=0; shift ;;
		--hub) BUILD=0; shift ;;
		--container) MODE=container; shift ;;
		--native) MODE=native; shift ;;
		--rebuild) REBUILD=1; shift ;;
		--reset-db) RESET_DB=1; shift ;;
		--env-file) ENV_FILE="$2"; shift 2 ;;
		--keep) KEEP=1; shift ;;
		--smoke) SMOKE=1; DURATION="5s"; SWEEP="1 8"; shift ;;
		--port) PORT="$2"; shift 2 ;;
		--runtime) RUNTIME="$2"; shift 2 ;;
		--duration) DURATION="$2"; shift 2 ;;
		--sweep) SWEEP="$2"; shift 2 ;;
		-h|--help) sed -n '2,28p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done

if [[ ${RESET_DB:-0} == 1 && $START == 0 ]]; then
	echo "error: --reset-db only works when this script starts the server (it resets the database first, then starts it)." >&2
	echo "       With --no-start, reset by hand, then start your server, then run the script:" >&2
	echo "         set -a; . ./.env; set +a; PGPASSWORD=\"\$DB_PASS\" psql -q -h \"\$DB_HOST\" -p \"\${DB_PORT:-5432}\" -U \"\$DB_USER\" -d \"\$DB_NAME\" -f init.sql" >&2
	exit 2
fi

BASE_URL="${BASE_URL:-http://127.0.0.1:$PORT}"
PROVIDER_USER="${PROVIDER_USER:-provider}"; PROVIDER_PASS="${PROVIDER_PASS:-1234@Abc!}"
USER_NAME="${USER_NAME:-user1}"; USER_PASS="${USER_PASS:-123Mm@p0}"
WHITELIST="${WHITELIST:-127.0.0.1,172.*,192.168.*,10.*}"

for t in k6 curl jq; do command -v "$t" >/dev/null || { echo "error: $t is required (macOS: brew install $t)" >&2; exit 2; }; done

OUT_DIR="$ROOT/docs/perf"; RAW="$OUT_DIR/raw/$LABEL"; mkdir -p "$RAW"
REPORT="$OUT_DIR/$LABEL.md"
TMP="$(mktemp -d)"; STATS_PID=""; SRV_PID=""
V=("$ROOT/valhalla.sh")
[[ -n $RUNTIME ]] && V+=(--runtime "$RUNTIME")
V+=(--name "$NAME" --port "$PORT")

cleanup() {
	[[ -n $STATS_PID ]] && kill "$STATS_PID" 2>/dev/null
	if [[ -n ${SRV_PID:-} && $KEEP == 0 ]]; then kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; fi
	if [[ $MODE == container && $START == 1 && $KEEP == 0 ]]; then "${V[@]}" delete -y >/dev/null 2>&1 || "${V[@]}" delete >/dev/null 2>&1 </dev/null; fi
	rm -rf "$TMP"
}
trap cleanup EXIT

log() { printf '\n== %s ==\n' "$*"; }

# ---- 1. server
SERVER_LOG="$RAW/server.log"
if [[ $START == 1 && $MODE == native ]]; then
	BIN="$ROOT/build/Release/server"
	if [[ $REBUILD == 1 ]]; then
		log "Building (cmake --build build/Release)"
		cmake --build "$ROOT/build/Release" || { echo "error: build failed" >&2; exit 1; }
	fi
	[[ -x $BIN ]] || { echo "error: $BIN not found; run ./build.sh or use --rebuild" >&2; exit 1; }
	log "Starting native server $BIN on port $PORT"
	# Load KEY=VALUE lines from the env file (never sourced, same parser idea as run_release.sh).
	if [[ -z ${ENV_FILE:-} ]]; then
		if [[ -f $ROOT/config ]]; then EF="$ROOT/config"
		elif [[ -f $ROOT/.env ]]; then EF="$ROOT/.env"
		else EF="$ROOT/config"
		fi
	else
		EF="$ENV_FILE"
	fi
	if [[ -f $EF ]]; then
		while IFS= read -r line || [[ -n $line ]]; do
			line=${line%$'\r'}
			[[ $line =~ ^[[:space:]]*(#|$) ]] && continue
			[[ $line =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]] || continue
			key=${BASH_REMATCH[2]}; val=${BASH_REMATCH[3]}
			val=${val%%[[:space:]]#*}; val=${val%"${val##*[![:space:]]}"}
			if [[ $val =~ ^\"(.*)\"$ || $val =~ ^\'(.*)\'$ ]]; then val=${BASH_REMATCH[1]}; fi
			export "$key=$val"
		done <"$EF"
	else
		echo "note: no env file at $EF, using the environment and built-in defaults"
	fi
	if [[ $RESET_DB == 1 ]]; then
		command -v psql >/dev/null || { echo "error: --reset-db needs psql" >&2; exit 1; }
		log "Resetting database ${DB_NAME:-postgres} on ${DB_HOST:-127.0.0.1}:${DB_PORT:-5432} (init.sql, drops all data)"
		PGPASSWORD="${DB_PASS:-}" psql -q -v ON_ERROR_STOP=1 -h "${DB_HOST:-127.0.0.1}" -p "${DB_PORT:-5432}" -U "${DB_USER:-postgres}" -d "${DB_NAME:-postgres}" -f "$ROOT/scripts/db/init.sql" >/dev/null \
			|| { echo "error: init.sql failed" >&2; exit 1; }
	fi
	# Perf-run overrides: our port, no rate-limit bans, quiet console (the log goes to $SERVER_LOG).
	export SERVER_PORT="$PORT" MAX_REQUESTS=100000000 MAX_FPS=100000000 WHITELIST="$WHITELIST" SERVER_LOG_TO_CONSOLE=false
	if lsof -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then echo "error: port $PORT is already in use; stop that server first" >&2; exit 1; fi
	mkdir -p "$ROOT/build/run"
	( cd "$ROOT/build/run" && exec "$BIN" ) >"$SERVER_LOG" 2>&1 &
	SRV_PID=$!
elif [[ $START == 1 ]]; then
	log "Starting server ($NAME on port $PORT)"
	printf 'WHITELIST=%s\nMAX_REQUESTS=100000000\nMAX_FPS=100000000\n' "$WHITELIST" >"$TMP/env"
	if [[ $BUILD == 1 ]]; then CMD=(build "$LABEL"); echo "Building the image from this checkout (first build can take several minutes)"; else CMD=(run); fi
	"${V[@]}" --env-file "$TMP/env" "${CMD[@]}" || { echo "error: could not start the server" >&2; exit 1; }
fi
for _ in $(seq 1 90); do
	curl -fsS --max-time 3 "$BASE_URL/api/v2/hello" >/dev/null 2>&1 && break
	if [[ -n $SRV_PID ]] && ! kill -0 "$SRV_PID" 2>/dev/null; then
		echo "error: the server exited. Last log lines ($SERVER_LOG):" >&2; tail -20 "$SERVER_LOG" >&2
		echo "hint: PostgreSQL must be running and initialised (psql -f init.sql via ./db_init.sh), matching DB_* in .env" >&2
		exit 1
	fi
	sleep 1
done
curl -fsS --max-time 3 "$BASE_URL/api/v2/hello" >/dev/null 2>&1 || { echo "error: server not answering at $BASE_URL" >&2; exit 1; }

# ---- 2. seed provider + user (ignore "already exists")
log "Seeding"
post() { curl -sS --max-time 15 -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -X POST "$BASE_URL/api/v2$1" -d "$2"; }
PROVIDER_JSON="$(jq -nc --arg u "$PROVIDER_USER" --arg p "$PROVIDER_PASS" '{avatar:"http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",active:true,language:"en",username:$u,password:$p,fullname:"Amr Nasr",phone:"+201299999999",email:"amr@mail.com",country:"Egypt",city:"Damietta",address:"portsaid street",dob:"17-02-1985",gender:"Male",position:"Doctor",speciality:"Cardiology"}')"
USER_JSON="$(jq -nc --arg u "$USER_NAME" --arg p "$USER_PASS" '{avatar:"http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",language:"en",active:true,parent:"0",username:$u,email:"amr@mail.com",password:$p,fullname:"Amr Nasr",phone:"+201299999999",country:"Egypt",city:"Damietta",address:"12 valhalla Street ",dob:"10-10-1990",gender:"Male",marital:"single",job:"Doctor"}')"
echo "provider create: $(post /clients/providers/create "$PROVIDER_JSON")   user create: $(post /clients/users/create "$USER_JSON")"
LOGIN="$(curl -sS --max-time 15 -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -X POST "$BASE_URL/api/v2/clients/providers/login" -d "$(jq -nc --arg u "$PROVIDER_USER" --arg p "$PROVIDER_PASS" '{username:$u,password:$p}')")"
[[ $LOGIN == 200 ]] || { echo "error: provider login returned $LOGIN (check seeding / rate limiter)" >&2; exit 1; }

export BASE_URL PROVIDER_USER PROVIDER_PASS USER_NAME USER_PASS

if [[ $START == 0 ]]; then
	echo "NOTE: --no-start needs a server started with MAX_REQUESTS=100000000, MAX_FPS=100000000 and a WHITELIST,"
	echo "      otherwise the rate limiter (default 10000 requests/30 s per IP) bans the load generator for an hour."
fi

# ---- 3. CPU/memory sampler (one snapshot every 5 s, never a streaming call)
if [[ -n $SRV_PID ]]; then
	( while kill -0 "$SRV_PID" 2>/dev/null; do
		ps -o %cpu=,rss= -p "$SRV_PID" | awk -v t="$(date +%H:%M:%S)" '{printf "%s %s%% %.0fMiB\n", t, $1, $2/1024}'
		sleep 5
	done ) >"$RAW/container-stats.txt" &
	STATS_PID=$!
elif [[ $MODE == container && $START == 1 && ( $RUNTIME == docker || ( -z $RUNTIME && -n $(command -v docker) ) ) ]]; then
	RT="${RUNTIME:-docker}"
	( while :; do
		"$RT" stats --no-stream --format '{{.CPUPerc}} {{.MemUsage}}' "$NAME" 2>/dev/null | sed "s/^/$(date +%H:%M:%S) /"
		sleep 5
	done ) >"$RAW/container-stats.txt" &
	STATS_PID=$!
fi

# ---- 4. concurrency sweep
log "Concurrency sweep (providers/read): $SWEEP, $DURATION each"
for n in $SWEEP; do
	echo "-- $n VUs for $DURATION ($(date +%H:%M:%S))"
	k6 run --quiet -e VUS="$n" -e DURATION="$DURATION" --summary-export "$RAW/sweep-$n.json" "$ROOT/tools/k6/sweep.js" >"$RAW/sweep-$n.log" 2>&1 \
		|| echo "   (k6 exited non-zero; see $RAW/sweep-$n.log)"
done

# ---- 5. full mixed benchmark
log "Mixed benchmark (reads + crud + auth), takes about 2 minutes, live log: $RAW/benchmark.log"
if [[ $SMOKE == 1 ]]; then export SMOKE=1; fi
k6 run --quiet --summary-export "$RAW/benchmark.json" "$ROOT/tools/k6/benchmark.js" >"$RAW/benchmark.log" 2>&1 \
	|| echo "   (k6 exited non-zero, usually a threshold; see $RAW/benchmark.log)"

# ---- 5b. login burst: do many simultaneous logins slow down plain reads?
log "Login burst (reads alone, then reads while clients log in), about 40 seconds"
# the mixed benchmark may have removed the provider account, so make sure it exists again (create is a no-op if it does)
post /clients/providers/create "$PROVIDER_JSON" >/dev/null || true
k6 run --quiet --summary-export "$RAW/login-burst.json" "$ROOT/tools/k6/login_burst.js" >"$RAW/login-burst.log" 2>&1 \
	|| echo "   (k6 exited non-zero; see $RAW/login-burst.log)"

# ---- 5c. slow query: does a query stuck in the database slow down unrelated reads?
# A background psql locks the users table during the second phase, so reads of that table wait inside the database while
# reads of the providers table go on. Needs psql and the same DB_* settings the server uses (native mode loads them from .env).
SLOW_DONE=0
if command -v psql >/dev/null; then
	SLOW_PHASE=15
	log "Slow query (reads alone, then reads while ${SLOW_VUS:-4} clients wait on a locked table), about 45 seconds"
	# the mixed benchmark removes the accounts it logs in as, so create both again (a no-op if they exist)
	post /clients/providers/create "$PROVIDER_JSON" >/dev/null || true
	post /clients/users/create "$USER_JSON" >/dev/null || true
	(
		sleep $((SLOW_PHASE + 5 + 1))
		PGPASSWORD="${DB_PASS:-}" psql -q -h "${DB_HOST:-127.0.0.1}" -p "${DB_PORT:-5432}" -U "${DB_USER:-postgres}" -d "${DB_NAME:-postgres}" \
			-c "BEGIN; LOCK TABLE users IN ACCESS EXCLUSIVE MODE; SELECT pg_sleep($((SLOW_PHASE - 1))); COMMIT;" >"$RAW/slow-query-lock.log" 2>&1
	) &
	LOCK_PID=$!
	k6 run --quiet -e PHASE="${SLOW_PHASE}s" --summary-export "$RAW/slow-query.json" "$ROOT/tools/k6/slow_query.js" >"$RAW/slow-query.log" 2>&1 \
		|| echo "   (k6 exited non-zero; see $RAW/slow-query.log)"
	wait "$LOCK_PID" 2>/dev/null
	SLOW_DONE=1
	if grep -qi "error\|fatal" "$RAW/slow-query-lock.log" 2>/dev/null; then echo "WARNING: the table lock failed, see $RAW/slow-query-lock.log"; SLOW_DONE=0; fi
else
	echo "note: psql not found, skipping the slow-query scenario"
fi

[[ -n $STATS_PID ]] && { kill "$STATS_PID" 2>/dev/null; STATS_PID=""; }

# A run where most requests failed measures the rate limiter, not the server: refuse to call it a result.
BAD=0
for f in "$RAW"/sweep-*.json "$RAW/benchmark.json"; do
	[[ -s $f ]] || continue
	r="$(jq -r '(.metrics.http_req_failed.value // .metrics.http_req_failed.rate // 0)' "$f" 2>/dev/null)"
	if awk -v r="$r" 'BEGIN{exit !(r > 0.05)}'; then echo "WARNING: $(basename "$f") failed rate $r (rate limiter ban / 429 / 503?)"; BAD=1; fi
done

# ---- 6. report
log "Writing $REPORT"
m() { jq -r --arg k "$2" "$3" "$1" 2>/dev/null || echo "n/a"; }   # m <file> <key> <jq>
burst_row() { # burst_row <label> <read scenario> <other scenario or empty> [json file]
	local f="${4:-$RAW/login-burst.json}"
	[[ -s $f ]] || { echo "| $1 | n/a | n/a | n/a | n/a |"; return; }
	jq -r --arg l "$1" --arg rs "$2" --arg ls "$3" '
		.metrics as $m |
		($m["http_req_duration{scenario:" + $rs + "}"] // {}) as $d |
		($m["http_reqs{scenario:" + $rs + "}"] // {}) as $q |
		(if $ls == "" then "-" else ($m["http_reqs{scenario:" + $ls + "}"].rate // 0 | . * 10 | round / 10 | tostring) end) as $lr |
		"| \($l) | \($q.rate // 0 | . * 10 | round / 10) | \($d["p(95)"] // 0 | . * 10 | round / 10) | \($d["p(99)"] // 0 | . * 10 | round / 10) | \($lr) |"
	' "$f"
}
row() { # row <label> <json>
	local f="$2"
	[[ -s $f ]] || { echo "| $1 | n/a | n/a | n/a | n/a | n/a | n/a |"; return; }
	jq -r --arg l "$1" '
		.metrics as $m |
		"| \($l) | \($m.http_reqs.rate // 0 | . * 10 | round / 10) | \($m.http_req_duration.med // 0 | . * 10 | round / 10) | \($m.http_req_duration["p(95)"] // 0 | . * 10 | round / 10) | \($m.http_req_duration["p(99)"] // 0 | . * 10 | round / 10) | \((($m.http_req_failed.value // $m.http_req_failed.rate // 0) * 10000 | round) / 100)% | \($m.http_reqs.count // 0) |"
	' "$f"
}
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
} >"$REPORT"

echo "Done. Report: $REPORT"
echo "Raw k6 output: $RAW"
