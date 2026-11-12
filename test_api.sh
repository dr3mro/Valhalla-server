#!/usr/bin/env bash
# End-to-end curl test of every Valhalla-server API endpoint (api/v2).
#
# Request bodies live in api_tests/payloads/<group>/<name>.json. They are templates: {{NAME}} is replaced
# with the shell variable NAME at run time (ids and tokens returned by earlier calls, run id, usernames...).
# Numbers are written bare ({{CLINIC_ID}}), strings keep their quotes ("{{USER_USERNAME}}").
#
# Operations run in dependency order, capturing ids with jq as they are returned:
#   hello -> provider -> user -> services -> clinic -> patient -> health/drugs/reports
#   -> visit -> requests/prescriptions/paidservices -> appointment -> suspend check -> teardown
#
# Usage:   ./test_api.sh
# Env:     BASE_URL (default http://127.0.0.1:8080/api/v2)   VERBOSE=1 (print every response)
#          PAYLOAD_DIR (default api_tests/payloads next to this script)   SKIP_SUSPEND=1   SAMPLES_OUT=file.jsonl (record responses for the docs)
#          PROVIDER_USERNAME / USER_USERNAME (default: unique per run, so reruns never clash)
# Note:    the server only implements DELETE for patients and visits; deleting health, drugs, reports,
#          requests, prescriptions and paidservices returns 400 by design (rows go away via ON DELETE CASCADE).
# Needs:   curl, jq. Exit code is 0 only if every step returned the expected status.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"
PAYLOAD_DIR="${PAYLOAD_DIR:-$ROOT/api_tests/payloads}"
VERBOSE="${VERBOSE:-0}"
RUN="$(date +%s | tail -c 7)"
PROVIDER_USERNAME="${PROVIDER_USERNAME:-provider${RUN}}"
USER_USERNAME="${USER_USERNAME:-user${RUN}}"
PROVIDER_PASSWORD='1234@Abc!'
USER_PASSWORD='123Mm@p0'

for tool in curl jq; do
	command -v "$tool" >/dev/null || { echo "error: $tool is required" >&2; exit 2; }
done
[[ -d $PAYLOAD_DIR ]] || { echo "error: payload directory not found: $PAYLOAD_DIR" >&2; exit 2; }

TMP="$(mktemp -d)"
PASS=0
FAIL=0
FAILED=""
TEARDOWN_DONE=0
LAST=""
STATUS=""
ID="" LOGIN_USERNAME="" LOGIN_PASSWORD="" DISPOSABLE_NAME=""
PROVIDER_ID="" PROVIDER_TOKEN="" USER_ID="" USER_TOKEN=""
CLINIC_ID="" PHARMACY_ID="" LAB_ID="" RAD_ID=""
PATIENT_ID="" HEALTH_ID="" DRUGS_ID="" REPORTS_ID=""
VISIT_ID="" REQUEST_ID="" PRESCRIPTION_ID="" PAIDSERVICE_ID="" APPOINTMENT_ID=""

if [[ -t 1 ]]; then G=$'\033[32m'; R=$'\033[31m'; B=$'\033[1m'; N=$'\033[0m'; else G=""; R=""; B=""; N=""; fi

section() { printf '\n%s== %s ==%s\n' "$B" "$1" "$N"; }

# body <group/name>: print payloads/<group>/<name>.json with every {{NAME}} replaced by $NAME, as compact JSON.
# Aborts the whole run (via SIGTERM to the main shell) when the file or a variable is missing or the result is not JSON.
body() {
	local file="$PAYLOAD_DIR/$1.json" content name out
	if [[ ! -f $file ]]; then echo "error: payload file missing: $file" >&2; kill -TERM $$; return 1; fi
	content="$(<"$file")"
	for name in $(printf '%s' "$content" | grep -o '{{[A-Z_]*}}' | sort -u | tr -d '{}'); do
		if [[ -z ${!name+x} ]]; then echo "error: $file uses {{$name}} but no such variable is set" >&2; kill -TERM $$; return 1; fi
		content="${content//"{{$name}}"/${!name}}"
	done
	if ! out="$(printf '%s' "$content" | jq -c . 2>&1)"; then
		echo "error: $file is not valid JSON after substitution: $out" >&2; kill -TERM $$; return 1
	fi
	printf '%s' "$out"
}
rd() { ID="$1"; body common/read; }   # rd <id>: standard {"id":..,"schema":[]} read body

# step "name" METHOD /path [token] [json-body | @empty] [expected-status]
# Records pass/fail, leaves the response in $LAST and the HTTP status in $STATUS.
step() {
	local name="$1" method="$2" path="$3" token="${4:-}" payload="${5:-}" expect="${6:-200}"
	local args=(-sS --max-time 20 -o "$TMP/body" -w '%{http_code}' -X "$method")
	[[ -n $token ]] && args+=(-H "Authorization: Bearer $token")
	if [[ $payload == "@empty" ]]; then
		args+=(-H "Content-Type: application/json")
	elif [[ -n $payload ]]; then
		args+=(-H "Content-Type: application/json" --data "$payload")
	fi
	: >"$TMP/body"
	STATUS="$(curl "${args[@]}" "$BASE_URL$path" 2>"$TMP/err")" || STATUS="000"
	LAST="$(cat "$TMP/body")"
	if [[ -n ${SAMPLES_OUT:-} ]]; then # optional: record real responses for the docs generator (tools/docs/build.sh)
		jq -cn --arg m "$method" --arg p "$path" --arg s "$STATUS" --arg b "$LAST" '{method:$m,path:$p,status:($s|tonumber),body:$b}' >>"$SAMPLES_OUT" 2>/dev/null || true
	fi
	if [[ $STATUS == "$expect" ]]; then
		PASS=$((PASS + 1))
		printf '  %s✓%s %s %-7s %s\n' "$G" "$N" "$STATUS" "$method" "$name"
		[[ $VERBOSE == 1 ]] && printf '      %s\n' "$(printf '%s' "$LAST" | head -c 400)"
	else
		FAIL=$((FAIL + 1))
		FAILED="${FAILED}  - ${name} (${method} ${path%%\?*}): got ${STATUS}, expected ${expect}"$'\n'
		printf '  %s✗%s %s %-7s %s (expected %s)\n' "$R" "$N" "$STATUS" "$method" "$name" "$expect"
		printf '      %s\n' "$(printf '%s' "$LAST" | head -c 400)"
		[[ -n $payload && $payload != "@empty" ]] && printf '      payload: %s\n' "$(printf '%s' "$payload" | head -c 1000)"
		[[ -s $TMP/err ]] && printf '      curl: %s\n' "$(cat "$TMP/err")"
	fi
	return 0
}

# setvar VAR 'jq-filter'  - store a field of the last response; abort the run if it is missing.
setvar() {
	local val
	val="$(printf '%s' "$LAST" | jq -r "$2 // empty" 2>/dev/null)"
	if [[ -z $val ]]; then
		echo "${R}  cannot read $2 from the last response, later steps depend on it - aborting${N}"
		exit 1
	fi
	printf -v "$1" '%s' "$val"
}

login() { # login <providers|users> <username> <password>
	LOGIN_USERNAME="$2"; LOGIN_PASSWORD="$3"
	step "$1 login" POST "/clients/$1/login" "" "$(body auth/login)"
}

# service_crud <pharmacies|laboratories|radiologycenters> <ID_VAR>
service_crud() {
	local type="$1" var="$2"
	step "$type create" POST "/services/$type/create" "$PROVIDER_TOKEN" "$(body "$type/create")"
	setvar "$var" '.id'
	step "$type read" POST "/services/$type/read" "$PROVIDER_TOKEN" "$(rd "${!var}")"
	step "$type update" PUT "/services/$type/update?id=${!var}" "$PROVIDER_TOKEN" "$(body "$type/update")"
	step "$type search" POST "/services/$type/search" "$PROVIDER_TOKEN" "$(body "$type/search")"
}

teardown() {
	[[ $TEARDOWN_DONE == 1 ]] && return
	TEARDOWN_DONE=1
	section "Teardown (reverse dependency order)"
	local pt="$PROVIDER_TOKEN" nd="(not implemented by design)"
	[[ -n $PAIDSERVICE_ID ]] && step "paidservices delete $nd" DELETE "/clinic/paidservices/delete?id=$VISIT_ID" "$pt" "" 400
	[[ -n $PRESCRIPTION_ID ]] && step "prescriptions delete $nd" DELETE "/clinic/prescriptions/delete?id=$VISIT_ID" "$pt" "" 400
	[[ -n $REQUEST_ID ]] && step "requests delete $nd" DELETE "/clinic/requests/delete?id=$VISIT_ID" "$pt" "" 400
	[[ -n $VISIT_ID ]] && step "visits delete" DELETE "/clinic/visits/delete?id=$VISIT_ID" "$pt"
	[[ -n $REPORTS_ID ]] && step "reports delete $nd" DELETE "/clinic/reports/delete?id=$PATIENT_ID" "$pt" "" 400
	[[ -n $DRUGS_ID ]] && step "patientdrugs delete $nd" DELETE "/clinic/patientdrugs/delete?id=$PATIENT_ID" "$pt" "" 400
	[[ -n $HEALTH_ID ]] && step "health delete $nd" DELETE "/clinic/health/delete?id=$PATIENT_ID" "$pt" "" 400
	[[ -n $PATIENT_ID ]] && step "patients delete" DELETE "/clinic/patients/delete?id=$PATIENT_ID" "$pt"
	[[ -n $APPOINTMENT_ID ]] && step "appointments delete" DELETE "/appointments/clinics/delete?id=$APPOINTMENT_ID" "$pt"
	[[ -n $CLINIC_ID ]] && step "clinics delete" DELETE "/services/clinics/delete?id=$CLINIC_ID" "$pt"
	[[ -n $PHARMACY_ID ]] && step "pharmacies delete" DELETE "/services/pharmacies/delete?id=$PHARMACY_ID" "$pt"
	[[ -n $LAB_ID ]] && step "laboratories delete" DELETE "/services/laboratories/delete?id=$LAB_ID" "$pt"
	[[ -n $RAD_ID ]] && step "radiologycenters delete" DELETE "/services/radiologycenters/delete?id=$RAD_ID" "$pt"

	if [[ -n $USER_ID ]]; then
		step "users logout" POST "/clients/users/logout" "$USER_TOKEN" "@empty"
		login users "$USER_USERNAME" "$USER_PASSWORD" # fresh token: the logout above invalidated the old one
		USER_TOKEN="$(printf '%s' "$LAST" | jq -r '.token // empty')"
		step "users delete" DELETE "/clients/users/delete?id=$USER_ID" "$USER_TOKEN"
	fi
	if [[ -n $PROVIDER_ID ]]; then
		step "providers logout" POST "/clients/providers/logout" "$PROVIDER_TOKEN" "@empty"
		login providers "$PROVIDER_USERNAME" "$PROVIDER_PASSWORD"
		PROVIDER_TOKEN="$(printf '%s' "$LAST" | jq -r '.token // empty')"
		step "providers delete" DELETE "/clients/providers/delete?id=$PROVIDER_ID" "$PROVIDER_TOKEN"
	fi
}

on_exit() {
	local code=$?
	teardown
	rm -rf "$TMP"
	printf '\n%sResult:%s %s passed, %s failed\n' "$B" "$N" "$PASS" "$FAIL"
	[[ -n $FAILED ]] && printf '%s' "$FAILED"
	[[ $FAIL -gt 0 || $code -ne 0 ]] && exit 1
	exit 0
}
trap on_exit EXIT
trap 'exit 1' TERM

echo "Target: $BASE_URL (run id $RUN)"
echo "Payloads: $PAYLOAD_DIR"

section "Health check"
step "hello" GET "/hello"
if [[ $STATUS == "000" ]]; then echo "${R}server is not reachable at $BASE_URL${N}"; exit 2; fi

section "Provider"
step "providers create" POST "/clients/providers/create" "" "$(body providers/create)"
setvar PROVIDER_ID '.id'
login providers "$PROVIDER_USERNAME" "$PROVIDER_PASSWORD"
setvar PROVIDER_TOKEN '.token'
setvar PROVIDER_ID '.client_id'
step "providers read" POST "/clients/providers/read" "$PROVIDER_TOKEN" "$(rd "$PROVIDER_ID")"
step "providers update" PUT "/clients/providers/update?id=$PROVIDER_ID" "$PROVIDER_TOKEN" "$(body providers/update)"
step "providers search" POST "/clients/providers/search" "$PROVIDER_TOKEN" "$(body providers/search)"
step "providers services" GET "/clients/providers/services?id=$PROVIDER_ID" "$PROVIDER_TOKEN"
step "providers activate" POST "/clients/providers/activate?id=$PROVIDER_ID" "$PROVIDER_TOKEN"

section "User"
step "users create" POST "/clients/users/create" "" "$(body users/create)"
setvar USER_ID '.id'
login users "$USER_USERNAME" "$USER_PASSWORD"
setvar USER_TOKEN '.token'
setvar USER_ID '.client_id'
step "users read" POST "/clients/users/read" "$USER_TOKEN" "$(rd "$USER_ID")"
step "users update" PUT "/clients/users/update?id=$USER_ID" "$USER_TOKEN" "$(body users/update)"
step "users search" POST "/clients/users/search" "$USER_TOKEN" "$(body users/search)"
step "users services" GET "/clients/users/services?id=$USER_ID" "$USER_TOKEN"
step "users activate" POST "/clients/users/activate?id=$USER_ID" "$USER_TOKEN"

section "Services: clinic"
step "clinics create" POST "/services/clinics/create" "$PROVIDER_TOKEN" "$(body clinics/create)"
setvar CLINIC_ID '.id'
step "clinics read" POST "/services/clinics/read" "$PROVIDER_TOKEN" "$(rd "$CLINIC_ID")"
step "clinics update" PUT "/services/clinics/update?id=$CLINIC_ID" "$PROVIDER_TOKEN" "$(body clinics/update)"
step "clinics search" POST "/services/clinics/search" "$PROVIDER_TOKEN" "$(body clinics/search)"

section "Services: pharmacy, laboratory, radiology center"
service_crud pharmacies PHARMACY_ID
service_crud laboratories LAB_ID
service_crud radiologycenters RAD_ID

section "Clinic: patient"
step "patients create" POST "/clinic/patients/create" "$PROVIDER_TOKEN" "$(body patients/create)"
setvar PATIENT_ID '.id'
step "patients read" POST "/clinic/patients/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "patients update" PUT "/clinic/patients/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body patients/update)"
step "patients search" POST "/clinic/patients/search" "$PROVIDER_TOKEN" "$(body patients/search)"
step "patients getvisits" GET "/clinic/patients/getvisits?patient_id=$PATIENT_ID" "$PROVIDER_TOKEN"

section "Clinic: patient health, drugs, reports"
step "health create" POST "/clinic/health/create" "$PROVIDER_TOKEN" "$(body health/create)"
setvar HEALTH_ID '.id'
step "health read" POST "/clinic/health/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "health update" PUT "/clinic/health/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body health/update)"

step "patientdrugs create" POST "/clinic/patientdrugs/create" "$PROVIDER_TOKEN" "$(body patientdrugs/create)"
setvar DRUGS_ID '.id'
step "patientdrugs read" POST "/clinic/patientdrugs/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "patientdrugs update" PUT "/clinic/patientdrugs/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body patientdrugs/update)"

step "reports create" POST "/clinic/reports/create" "$PROVIDER_TOKEN" "$(body reports/create)"
setvar REPORTS_ID '.id'
step "reports read" POST "/clinic/reports/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "reports update" PUT "/clinic/reports/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body reports/update)"

section "Clinic: visit and its requests, prescriptions, paid services"
step "visits create" POST "/clinic/visits/create" "$PROVIDER_TOKEN" "$(body visits/create)"
setvar VISIT_ID '.id'
step "visits read" POST "/clinic/visits/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "visits update" PUT "/clinic/visits/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body visits/update)"

step "requests create" POST "/clinic/requests/create" "$PROVIDER_TOKEN" "$(body requests/create)"
setvar REQUEST_ID '.id'
step "requests read" POST "/clinic/requests/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "requests update" PUT "/clinic/requests/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body requests/update)"

step "prescriptions create" POST "/clinic/prescriptions/create" "$PROVIDER_TOKEN" "$(body prescriptions/create)"
setvar PRESCRIPTION_ID '.id'
step "prescriptions read" POST "/clinic/prescriptions/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "prescriptions update" PUT "/clinic/prescriptions/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body prescriptions/update)"

step "paidservices create" POST "/clinic/paidservices/create" "$PROVIDER_TOKEN" "$(body paidservices/create)"
setvar PAIDSERVICE_ID '.id'
step "paidservices read" POST "/clinic/paidservices/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "paidservices update" PUT "/clinic/paidservices/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body paidservices/update)"

section "Appointments (needs clinic, user and provider)"
step "appointments create" POST "/appointments/clinics/create" "$PROVIDER_TOKEN" "$(body appointments/create)"
setvar APPOINTMENT_ID '.id'
step "appointments read" POST "/appointments/clinics/read?id=$APPOINTMENT_ID" "$PROVIDER_TOKEN" "$(body appointments/read)"
step "appointments update" PUT "/appointments/clinics/update?id=$APPOINTMENT_ID" "$PROVIDER_TOKEN" "$(body appointments/update)"

# Suspend locks an account out of every endpoint (even activate and delete), so it cannot be tested on the
# accounts used above. It runs last on two disposable accounts, which stay suspended afterwards.
if [[ ${SKIP_SUSPEND:-0} != 1 ]]; then
	section "Suspend (disposable accounts, left suspended)"
	for group in providers users; do
		DISPOSABLE_NAME="${group%s}x${RUN}"
		step "$group create (disposable)" POST "/clients/$group/create" "" "$(body "$group/disposable")"
		setvar sid '.id'
		login "$group" "$DISPOSABLE_NAME" "$USER_PASSWORD"
		setvar stok '.token'
		step "$group activate" POST "/clients/$group/activate?id=$sid" "$stok"
		step "$group suspend" POST "/clients/$group/suspend?id=$sid" "$stok"
		step "$group blocked while suspended" POST "/clients/$group/read" "$stok" "$(rd "$sid")" 401
	done
fi

# Teardown (deletes, logout, re-login, final deletes) and the summary run from the EXIT trap,
# so created data is cleaned up even when a step aborts the run.
