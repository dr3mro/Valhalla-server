#!/usr/bin/env bash
# Step 0 of PERFORMANCE_PLAN.md: record a repeatable performance baseline.
#
# What it does:
#   1. starts a throw-away Valhalla container (./valhalla.sh run) unless --no-start is given
#   2. seeds a provider and a user (the k6 benchmark logs in as them)
#   3. runs a concurrency sweep on one read endpoint (tools/k6/sweep.js) at 1/8/32/128 VUs
#   4. runs the full mixed benchmark (tools/k6/benchmark.js: reads + crud + auth)
#   5. samples container CPU/memory while it runs
#   6. writes docs/perf/<label>.md (default: baseline.md) plus raw k6 JSON under docs/perf/raw/<label>/
#
# Usage:  tools/perf_baseline.sh [options]
#   --label NAME       output name (default: baseline). Use e.g. step3-permission-cache for later runs.
#   --no-start         use an already running server at BASE_URL (you handle rate limits and seeding state)
#   --keep             leave the container running afterwards
#   --smoke            short run (sweep 1 and 8 VUs for 5s, benchmark in SMOKE mode) to test the script
#   --port N           host port for the container (default 18080)
#   --runtime R        docker | container (default: valhalla.sh decides; env VALHALLA_RUNTIME also works)
#   --duration D       time per sweep level (default 20s)
#   --sweep "1 8 32"   VU levels for the sweep (default "1 8 32 128")
# Env: BASE_URL (default http://127.0.0.1:<port>), PROVIDER_USER/PROVIDER_PASS, USER_NAME/USER_PASS,
#      WHITELIST (IP patterns for the rate limiter, default "127.0.0.1,172.*,192.168.*,10.*")
# Needs: k6, curl, jq (and the container runtime unless --no-start).
# Note: the rate limiter / ban list will return 429/403 under load; if you see them in the results,
#       widen WHITELIST (see the RL_* / BAN_* / WHITELIST notes in valhalla.sh).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LABEL="baseline"; START=1; KEEP=0; SMOKE=0; PORT=18080; RUNTIME="${RUNTIME:-}"
DURATION="20s"; SWEEP="1 8 32 128"
NAME="perf-$$"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--label) LABEL="$2"; shift 2 ;;
		--no-start) START=0; shift ;;
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

BASE_URL="${BASE_URL:-http://127.0.0.1:$PORT}"
PROVIDER_USER="${PROVIDER_USER:-provider}"; PROVIDER_PASS="${PROVIDER_PASS:-1234@Abc!}"
USER_NAME="${USER_NAME:-user1}"; USER_PASS="${USER_PASS:-123Mm@p0}"
WHITELIST="${WHITELIST:-127.0.0.1,172.*,192.168.*,10.*}"

for t in k6 curl jq; do command -v "$t" >/dev/null || { echo "error: $t is required (macOS: brew install $t)" >&2; exit 2; }; done

OUT_DIR="$ROOT/docs/perf"; RAW="$OUT_DIR/raw/$LABEL"; mkdir -p "$RAW"
REPORT="$OUT_DIR/$LABEL.md"
TMP="$(mktemp -d)"; STATS_PID=""
V=("$ROOT/valhalla.sh")
[[ -n $RUNTIME ]] && V+=(--runtime "$RUNTIME")
V+=(--name "$NAME" --port "$PORT")

cleanup() {
	[[ -n $STATS_PID ]] && kill "$STATS_PID" 2>/dev/null
	if [[ $START == 1 && $KEEP == 0 ]]; then "${V[@]}" delete -y >/dev/null 2>&1 || "${V[@]}" delete >/dev/null 2>&1 </dev/null; fi
	rm -rf "$TMP"
}
trap cleanup EXIT

log() { printf '\n== %s ==\n' "$*"; }

# ---- 1. server
if [[ $START == 1 ]]; then
	log "Starting server ($NAME on port $PORT)"
	printf 'WHITELIST=%s\n' "$WHITELIST" >"$TMP/env"
	"${V[@]}" --env-file "$TMP/env" run || { echo "error: could not start the server" >&2; exit 1; }
fi
for _ in $(seq 1 60); do
	curl -fsS "$BASE_URL/api/v2/hello" >/dev/null 2>&1 && break
	sleep 1
done
curl -fsS "$BASE_URL/api/v2/hello" >/dev/null 2>&1 || { echo "error: server not answering at $BASE_URL" >&2; exit 1; }

# ---- 2. seed provider + user (ignore "already exists")
log "Seeding"
post() { curl -sS -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -X POST "$BASE_URL/api/v2$1" -d "$2"; }
PROVIDER_JSON="$(jq -nc --arg u "$PROVIDER_USER" --arg p "$PROVIDER_PASS" '{avatar:"http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",active:true,language:"en",username:$u,password:$p,fullname:"Amr Nasr",phone:"+201299999999",email:"amr@mail.com",country:"Egypt",city:"Damietta",address:"portsaid street",dob:"17-02-1985",gender:"Male",position:"Doctor",speciality:"Cardiology"}')"
USER_JSON="$(jq -nc --arg u "$USER_NAME" --arg p "$USER_PASS" '{avatar:"http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200",language:"en",active:true,parent:"0",username:$u,email:"amr@mail.com",password:$p,fullname:"Amr Nasr",phone:"+201299999999",country:"Egypt",city:"Damietta",address:"12 valhalla Street ",dob:"10-10-1990",gender:"Male",marital:"single",job:"Doctor"}')"
echo "provider create: $(post /clients/providers/create "$PROVIDER_JSON")   user create: $(post /clients/users/create "$USER_JSON")"
LOGIN="$(curl -sS -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -X POST "$BASE_URL/api/v2/clients/providers/login" -d "$(jq -nc --arg u "$PROVIDER_USER" --arg p "$PROVIDER_PASS" '{username:$u,password:$p}')")"
[[ $LOGIN == 200 ]] || { echo "error: provider login returned $LOGIN (check seeding / rate limiter)" >&2; exit 1; }

export BASE_URL PROVIDER_USER PROVIDER_PASS USER_NAME USER_PASS

# ---- 3. container stats sampler (one snapshot every 5 s, never a streaming call)
if [[ $START == 1 && ( $RUNTIME == docker || ( -z $RUNTIME && -n $(command -v docker) ) ) ]]; then
	RT="${RUNTIME:-docker}"
	( while :; do
		"$RT" stats --no-stream --format '{{.CPUPerc}} {{.MemUsage}}' "$NAME" 2>/dev/null | sed "s/^/$(date +%H:%M:%S) /"
		sleep 5
	done ) >"$RAW/container-stats.txt" &
	STATS_PID=$!
fi

# ---- 4. concurrency sweep
log "Concurrency sweep (providers/read): $SWEEP"
for n in $SWEEP; do
	echo "-- $n VUs for $DURATION"
	k6 run --quiet -e VUS="$n" -e DURATION="$DURATION" --summary-export "$RAW/sweep-$n.json" "$ROOT/tools/k6/sweep.js" >"$RAW/sweep-$n.log" 2>&1 \
		|| echo "   (k6 exited non-zero; see $RAW/sweep-$n.log)"
done

# ---- 5. full mixed benchmark
log "Mixed benchmark (reads + crud + auth)"
if [[ $SMOKE == 1 ]]; then export SMOKE=1; fi
k6 run --quiet --summary-export "$RAW/benchmark.json" "$ROOT/tools/k6/benchmark.js" >"$RAW/benchmark.log" 2>&1 \
	|| echo "   (k6 exited non-zero, usually a threshold; see $RAW/benchmark.log)"

[[ -n $STATS_PID ]] && { kill "$STATS_PID" 2>/dev/null; STATS_PID=""; }

# ---- 6. report
log "Writing $REPORT"
m() { jq -r --arg k "$2" "$3" "$1" 2>/dev/null || echo "n/a"; }   # m <file> <key> <jq>
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
	echo "- Target: $BASE_URL ($([[ $START == 1 ]] && echo "fresh ${RUNTIME:-default-runtime} container, default settings" || echo "existing server"))"
	echo "- Sweep: ${DURATION} per level, endpoint POST /clients/providers/read (auth + permission check + 1 DB read)"
	[[ $SMOKE == 1 ]] && echo "- **Smoke run: numbers are not meaningful**"
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
