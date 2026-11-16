#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file test_api.sh
#  * @brief End-to-end curl test of every api/v2 endpoint in dependency order with teardown.
#  * @details This script tests every API endpoint end to end with curl, in dependency order.
#  * It fills JSON templates with ids and tokens captured from earlier calls, then checks each status.
#  * At the end it deletes what it created and prints a pass/fail summary.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the main end-to-end check run after building and starting the server.
#  * A developer or CI runs it to confirm the whole API still works together.
#  * Without it broken endpoint wiring or changed response shapes would only be found by hand.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - The curl command: https://curl.se/docs/manpage.html
#  * - jq JSON processor: https://jqlang.github.io/jq/manual/
#  * - Bash trap command: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# End-to-end curl test of every Valhalla-server API endpoint (api/v2).
#
# Request bodies live in tests/api_tests/payloads/<group>/<name>.json. They are templates: {{NAME}} is replaced
# with the shell variable NAME at run time (ids and tokens returned by earlier calls, run id, usernames...).
# Numbers are written bare ({{CLINIC_ID}}), strings keep their quotes ("{{USER_USERNAME}}").
#
# Operations run in dependency order, capturing ids with jq as they are returned:
#   hello -> provider -> user -> services -> clinic -> patient -> health/drugs/reports
#   -> visit -> requests/prescriptions/paidservices -> appointment -> suspend check -> teardown
#
# Usage:   ./test_api.sh
# Env:     BASE_URL (default http://127.0.0.1:8080/api/v2)   VERBOSE=1 (print every response)
#          PAYLOAD_DIR (default tests/api_tests/payloads next to this script)   SKIP_SUSPEND=1   SAMPLES_OUT=file.jsonl (record responses for the docs)
#          PROVIDER_USERNAME / USER_USERNAME (default: unique per run, so reruns never clash)
# Note:    the server only implements DELETE for patients and visits; deleting health, drugs, reports,
#          requests, prescriptions and paidservices returns 400 by design (rows go away via ON DELETE CASCADE).
# Needs:   curl, jq. Exit code is 0 only if every step returned the expected status.
# Payload placeholders ({{ID}}, {{LOGIN_USERNAME}}) and setvar read/write
# variables by name, which shellcheck can't follow.
# shellcheck disable=SC2034,SC2154
# No `-e`: failures are tallied per step; the EXIT trap still tears down and prints the summary.
set -uo pipefail  # Treat unset variables and failed pipeline steps as errors.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # Absolute repository root, found from this file's own location.
BASE_URL="${BASE_URL:-http://127.0.0.1:8080/api/v2}"  # Server address to test; defaults to a local server.
PAYLOAD_DIR="${PAYLOAD_DIR:-$ROOT/tests/api_tests/payloads}"  # Folder of JSON request templates used below.
VERBOSE="${VERBOSE:-0}"  # When 1, print a snippet of every response body.
# Per-run suffix keeps generated usernames unique so repeated runs never collide.
RUN="$(date +%s | tail -c 7)"  # Short unique suffix so usernames do not clash between runs.
PROVIDER_USERNAME="${PROVIDER_USERNAME:-provider${RUN}}"  # Throwaway provider name built from the unique suffix.
USER_USERNAME="${USER_USERNAME:-user${RUN}}"  # Throwaway user name built from the unique suffix.
# Fixed fixture passwords for these throwaway local accounts; no override is supported.
PROVIDER_PASSWORD='1234@Abc!'  # Fixed fixture password for the throwaway provider.
USER_PASSWORD='123Mm@p0'  # Fixed fixture password for the throwaway user.

for tool in curl jq; do  # Check that each required command-line tool is installed.
	command -v "$tool" >/dev/null || { echo "error: $tool is required" >&2; exit 2; }  # Stop with a clear message if a tool is missing.
done
[[ -d $PAYLOAD_DIR ]] || { echo "error: payload directory not found: $PAYLOAD_DIR" >&2; exit 2; }  # Stop if the templates folder is missing.

# Private temp dir holds the current step's response body; removed by the EXIT trap.
TMP="$(mktemp -d)"  # Private temporary folder for the current response body.
# Accumulators for the final summary; TEARDOWN_DONE makes cleanup idempotent.
PASS=0  # Count of checks that returned the expected status.
FAIL=0  # Count of checks that did not.
FAILED=""  # Human-readable list of the failed steps for the final report.
TEARDOWN_DONE=0  # Guard so cleanup runs only once.
# LAST/STATUS always describe the most recent step, consumed by setvar and the suspend checks.
LAST=""  # Body of the most recent response.
STATUS=""  # HTTP status of the most recent response.
ID="" LOGIN_USERNAME="" LOGIN_PASSWORD="" DISPOSABLE_NAME=""  # Template variables used when expanding JSON request bodies.
PROVIDER_ID="" PROVIDER_TOKEN="" USER_ID="" USER_TOKEN=""  # Ids and tokens of the two main throwaway accounts.
CLINIC_ID="" PHARMACY_ID="" LAB_ID="" RAD_ID=""  # Ids of the service records created during the run.
PATIENT_ID="" HEALTH_ID="" DRUGS_ID="" REPORTS_ID=""  # Ids of the patient records created during the run.
VISIT_ID="" REQUEST_ID="" PRESCRIPTION_ID="" PAIDSERVICE_ID="" APPOINTMENT_ID=""  # Ids of the visit and appointment records created during the run.

if [[ -t 1 ]]; then G=$'\033[32m'; R=$'\033[31m'; B=$'\033[1m'; N=$'\033[0m'; else G=""; R=""; B=""; N=""; fi

# @brief Print a bold section banner.
# @param $1 Section title.
section() { printf '\n%s== %s ==%s\n' "$B" "$1" "$N"; }

# body <group/name>: print payloads/<group>/<name>.json with every {{NAME}} replaced by $NAME, as compact JSON.
# Aborts the whole run (via SIGTERM to the main shell) when the file or a variable is missing or the result is not JSON.
# @brief Expand a JSON template with variables captured earlier in the run.
# @param $1 Payload path relative to PAYLOAD_DIR, without the .json suffix.
# @return 0 with compact JSON on stdout; never returns on error (SIGTERM drives the EXIT trap).
# @note Uses indirect expansion (${!name}), which shellcheck cannot follow.
body() {
	local file="$PAYLOAD_DIR/$1.json" content name out  # Path to the template plus variables for its text, each placeholder and the result.
	if [[ ! -f $file ]]; then echo "error: payload file missing: $file" >&2; kill -TERM $$; return 1; fi  # A missing template aborts the whole run through the TERM trap.
	content="$(<"$file")"  # Read the whole template file into a variable.
	for name in $(printf '%s' "$content" | grep -o '{{[A-Z_]*}}' | sort -u | tr -d '{}'); do  # Find every {{PLACEHOLDER}} used in this template.
		if [[ -z ${!name+x} ]]; then echo "error: $file uses {{$name}} but no such variable is set" >&2; kill -TERM $$; return 1; fi  # Abort if a placeholder has no matching variable set.
		content="${content//"{{$name}}"/${!name}}"  # Replace the placeholder with the variable's current value.
	done
	if ! out="$(printf '%s' "$content" | jq -c . 2>&1)"; then  # Check that the filled-in template is still valid JSON.
		echo "error: $file is not valid JSON after substitution: $out" >&2; kill -TERM $$; return 1  # Abort with jq's error message if the JSON is broken.
	fi
	printf '%s' "$out"  # Hand the compact JSON back to the caller.
}
# @brief Build the standard read body for a given id by setting the ID template variable.
# @param $1 Entity id to read.
rd() { ID="$1"; body common/read; }   # rd <id>: standard {"id":..,"schema":[]} read body

# step "name" METHOD /path [token] [json-body | @empty] [expected-status]
# Records pass/fail, leaves the response in $LAST and the HTTP status in $STATUS.
# @brief Execute one API call and compare its status with the expectation.
# @param $1 Check name; $2 HTTP method; $3 path appended to BASE_URL.
# @param $4 Optional bearer token; $5 JSON body ("@empty" sends the header with no body).
# @param $6 Expected HTTP status (default 200).
# @note A transport failure is recorded as status "000"; SAMPLES_OUT, when set, appends a JSONL record.
step() {
	local name="$1" method="$2" path="$3" token="${4:-}" payload="${5:-}" expect="${6:-200}"  # Give readable names to the six arguments of a step.
	local args=(-sS --max-time 20 -o "$TMP/body" -w '%{http_code}' -X "$method")  # Build the base curl options: quiet, with a timeout, saving body and status.
	[[ -n $token ]] && args+=(-H "Authorization: Bearer $token")  # Add the bearer token when this call needs authentication.
	if [[ $payload == "@empty" ]]; then  # A special marker means send the header but no body.
		args+=(-H "Content-Type: application/json")  # Declare that the (empty) body is JSON.
	elif [[ -n $payload ]]; then  # Otherwise add a body only when one was supplied.
		args+=(-H "Content-Type: application/json" --data "$payload")  # Send the JSON body with the correct content type.
	fi
	: >"$TMP/body"  # Clear the body file so a failed curl leaves no stale data.
	STATUS="$(curl "${args[@]}" "$BASE_URL$path" 2>"$TMP/err")" || STATUS="000"  # Make the call and record status 000 if curl could not reach the server.
	LAST="$(cat "$TMP/body")"  # Keep the response text for later steps to read.
	if [[ -n ${SAMPLES_OUT:-} ]]; then # optional: record real responses for the docs generator (tools/docs/build.sh)
		jq -cn --arg m "$method" --arg p "$path" --arg s "$STATUS" --arg b "$LAST" '{method:$m,path:$p,status:($s|tonumber),body:$b}' >>"$SAMPLES_OUT" 2>/dev/null || true  # Optionally record this response as JSON lines for the documentation generator.
	fi
	if [[ $STATUS == "$expect" ]]; then  # Compare the actual status with the expected one.
		PASS=$((PASS + 1))  # Count a successful check.
		printf '  %s✓%s %s %-7s %s\n' "$G" "$N" "$STATUS" "$method" "$name"
		[[ $VERBOSE == 1 ]] && printf '      %s\n' "$(printf '%s' "$LAST" | head -c 400)"
	else
		FAIL=$((FAIL + 1))  # Count a failed check.
		FAILED="${FAILED}  - ${name} (${method} ${path%%\?*}): got ${STATUS}, expected ${expect}"$'\n'  # Remember the failure with its name, method and path for the summary.
		printf '  %s✗%s %s %-7s %s (expected %s)\n' "$R" "$N" "$STATUS" "$method" "$name" "$expect"
		printf '      %s\n' "$(printf '%s' "$LAST" | head -c 400)"
		[[ -n $payload && $payload != "@empty" ]] && printf '      payload: %s\n' "$(printf '%s' "$payload" | head -c 1000)"
		[[ -s $TMP/err ]] && printf '      curl: %s\n' "$(cat "$TMP/err")"
	fi
	return 0  # A failed HTTP check does not stop the rest of the script.
}

# setvar VAR 'jq-filter'  - store a field of the last response; abort the run if it is missing.
# @brief Capture a value from $LAST into a named variable for later steps.
# @param $1 Variable name to assign (via printf -v).
# @param $2 jq filter applied to the last response; an empty result aborts the whole run.
setvar() {
	local val  # Variable to hold the value pulled out of the last response.
	val="$(printf '%s' "$LAST" | jq -r "$2 // empty" 2>/dev/null)"  # Use jq to read the requested field, treating null as empty.
	if [[ -z $val ]]; then  # A missing value means later steps cannot work, so stop.
		echo "${R}  cannot read $2 from the last response, later steps depend on it - aborting${N}"  # Explain which field was missing.
		exit 1
	fi
	printf -v "$1" '%s' "$val"  # Store the value in the variable named by the caller.
}

# @brief Log in and leave the response (including the token) in $LAST for setvar.
# @param $1 Account type (providers or users); $2 username; $3 password fixture.
login() { # login <providers|users> <username> <password>
	LOGIN_USERNAME="$2"; LOGIN_PASSWORD="$3"  # Remember which account is logging in.
	step "$1 login" POST "/clients/$1/login" "" "$(body auth/login)"  # Call the login endpoint using the shared auth template.
}

# service_crud <pharmacies|laboratories|radiologycenters> <ID_VAR>
# @brief Run create/read/update/search for one service type, saving the created id in ID_VAR.
# @param $1 Service type (used in both endpoint path and payload directory); $2 variable receiving the id.
service_crud() {
	local type="$1" var="$2"  # Service type to test and the variable that will hold its id.
	step "$type create" POST "/services/$type/create" "$PROVIDER_TOKEN" "$(body "$type/create")"  # Create a service record and keep its id.
	setvar "$var" '.id'  # Save the new id under the variable name supplied by the caller.
	step "$type read" POST "/services/$type/read" "$PROVIDER_TOKEN" "$(rd "${!var}")"  # Read that service back to confirm it exists.
	step "$type update" PUT "/services/$type/update?id=${!var}" "$PROVIDER_TOKEN" "$(body "$type/update")"  # Update the service using the saved id.
	step "$type search" POST "/services/$type/search" "$PROVIDER_TOKEN" "$(body "$type/search")"  # Search the service list to confirm it is indexed.
}

# @brief Delete created records and accounts in reverse dependency order; safe to call more than once.
# @note Deletes the server does not implement are expected to return 400 by design.
# @note Logout invalidates the token, so a fresh login is required before deleting each account.
teardown() {
	[[ $TEARDOWN_DONE == 1 ]] && return  # Run cleanup only once even if called twice.
	TEARDOWN_DONE=1  # Mark cleanup as done straight away.
	section "Teardown (reverse dependency order)"  # Show that cleanup is starting.
	local pt="$PROVIDER_TOKEN" nd="(not implemented by design)"  # Keep the token and a short note for deletes that are expected to fail.
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

	if [[ -n $USER_ID ]]; then  # Only clean up the user if one was created.
		step "users logout" POST "/clients/users/logout" "$USER_TOKEN" "@empty"  # Log the user out before deleting it.
		login users "$USER_USERNAME" "$USER_PASSWORD" # fresh token: the logout above invalidated the old one
		USER_TOKEN="$(printf '%s' "$LAST" | jq -r '.token // empty')"  # Get a fresh token because logout invalidated the old one.
		step "users delete" DELETE "/clients/users/delete?id=$USER_ID" "$USER_TOKEN"  # Delete the throwaway user account.
	fi
	if [[ -n $PROVIDER_ID ]]; then  # Only clean up the provider if one was created.
		step "providers logout" POST "/clients/providers/logout" "$PROVIDER_TOKEN" "@empty"  # Log the provider out before deleting it.
		login providers "$PROVIDER_USERNAME" "$PROVIDER_PASSWORD"  # Log in as the new provider.
		PROVIDER_TOKEN="$(printf '%s' "$LAST" | jq -r '.token // empty')"  # Get a fresh provider token after logout.
		step "providers delete" DELETE "/clients/providers/delete?id=$PROVIDER_ID" "$PROVIDER_TOKEN"  # Delete the throwaway provider account.
	fi
}

# @brief EXIT trap: tear down resources, remove the temp dir, print the tally and choose the exit status.
# @note Preserves the original exit code so an aborted run cannot be reported as success.
on_exit() {
	local code=$?  # Remember the exit code that triggered this trap.
	teardown  # Delete everything this run created.
	rm -rf "$TMP"  # Remove the temporary response folder.
	printf '\n%sResult:%s %s passed, %s failed\n' "$B" "$N" "$PASS" "$FAIL"  # Print how many checks passed and failed.
	[[ -n $FAILED ]] && printf '%s' "$FAILED"  # List any failed steps.
	[[ $FAIL -gt 0 || $code -ne 0 ]] && exit 1  # Fail the run if a step failed or the script already errored.
	exit 0  # Report success when every check passed.
}
# EXIT covers normal completion and errors; TERM (sent by body/setvar on a missing dependency) exits via that path.
trap on_exit EXIT  # Run cleanup and the summary on every normal or error exit.
trap 'exit 1' TERM  # Turn the TERM signal (used on a missing dependency) into a failing exit.

echo "Target: $BASE_URL (run id $RUN)"  # Show which server and run this is.
echo "Payloads: $PAYLOAD_DIR"  # Show where the request templates came from.

section "Health check"  # Begin with the simplest possible call.
step "hello" GET "/hello"  # Check the server answers at all.
# "000" means curl got no response, so no other step can succeed; stop before creating anything.
if [[ $STATUS == "000" ]]; then echo "${R}server is not reachable at $BASE_URL${N}"; exit 2; fi  # Stop before creating anything if the server is unreachable.

section "Provider"
step "providers create" POST "/clients/providers/create" "" "$(body providers/create)"
setvar PROVIDER_ID '.id'  # Capture the provider id returned by create.
login providers "$PROVIDER_USERNAME" "$PROVIDER_PASSWORD"
setvar PROVIDER_TOKEN '.token'  # Capture the login token.
# The login response's client_id is the authoritative id used by later authenticated calls.
setvar PROVIDER_ID '.client_id'  # Use the authoritative id from the login response.
step "providers read" POST "/clients/providers/read" "$PROVIDER_TOKEN" "$(rd "$PROVIDER_ID")"
step "providers update" PUT "/clients/providers/update?id=$PROVIDER_ID" "$PROVIDER_TOKEN" "$(body providers/update)"
step "providers search" POST "/clients/providers/search" "$PROVIDER_TOKEN" "$(body providers/search)"
step "providers services" GET "/clients/providers/services?id=$PROVIDER_ID" "$PROVIDER_TOKEN"
step "providers activate" POST "/clients/providers/activate?id=$PROVIDER_ID" "$PROVIDER_TOKEN"

section "User"
step "users create" POST "/clients/users/create" "" "$(body users/create)"
setvar USER_ID '.id'  # Capture the user id returned by create.
login users "$USER_USERNAME" "$USER_PASSWORD"  # Log in as the new user.
setvar USER_TOKEN '.token'  # Capture the user's login token.
# Mirror the provider flow: replace the create-time id with the login-time client_id.
setvar USER_ID '.client_id'  # Use the authoritative id from the user's login response.
step "users read" POST "/clients/users/read" "$USER_TOKEN" "$(rd "$USER_ID")"
step "users update" PUT "/clients/users/update?id=$USER_ID" "$USER_TOKEN" "$(body users/update)"
step "users search" POST "/clients/users/search" "$USER_TOKEN" "$(body users/search)"
step "users services" GET "/clients/users/services?id=$USER_ID" "$USER_TOKEN"
step "users activate" POST "/clients/users/activate?id=$USER_ID" "$USER_TOKEN"

section "Services: clinic"
step "clinics create" POST "/services/clinics/create" "$PROVIDER_TOKEN" "$(body clinics/create)"
setvar CLINIC_ID '.id'  # Remember the new clinic's id.
step "clinics read" POST "/services/clinics/read" "$PROVIDER_TOKEN" "$(rd "$CLINIC_ID")"
step "clinics update" PUT "/services/clinics/update?id=$CLINIC_ID" "$PROVIDER_TOKEN" "$(body clinics/update)"
step "clinics search" POST "/services/clinics/search" "$PROVIDER_TOKEN" "$(body clinics/search)"

section "Services: pharmacy, laboratory, radiology center"
service_crud pharmacies PHARMACY_ID  # Run create/read/update/search for pharmacies.
service_crud laboratories LAB_ID  # Run the same flow for laboratories.
service_crud radiologycenters RAD_ID  # Run the same flow for radiology centers.

section "Clinic: patient"
step "patients create" POST "/clinic/patients/create" "$PROVIDER_TOKEN" "$(body patients/create)"
setvar PATIENT_ID '.id'  # Remember the new patient's id.
step "patients read" POST "/clinic/patients/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "patients update" PUT "/clinic/patients/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body patients/update)"
step "patients search" POST "/clinic/patients/search" "$PROVIDER_TOKEN" "$(body patients/search)"
step "patients getvisits" GET "/clinic/patients/getvisits?patient_id=$PATIENT_ID" "$PROVIDER_TOKEN"

section "Clinic: patient health, drugs, reports"
step "health create" POST "/clinic/health/create" "$PROVIDER_TOKEN" "$(body health/create)"
setvar HEALTH_ID '.id'  # Remember the new health record's id.
step "health read" POST "/clinic/health/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "health update" PUT "/clinic/health/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body health/update)"

step "patientdrugs create" POST "/clinic/patientdrugs/create" "$PROVIDER_TOKEN" "$(body patientdrugs/create)"
setvar DRUGS_ID '.id'  # Remember the new patient-drugs record's id.
step "patientdrugs read" POST "/clinic/patientdrugs/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "patientdrugs update" PUT "/clinic/patientdrugs/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body patientdrugs/update)"

step "reports create" POST "/clinic/reports/create" "$PROVIDER_TOKEN" "$(body reports/create)"
setvar REPORTS_ID '.id'  # Remember the new reports record's id.
step "reports read" POST "/clinic/reports/read" "$PROVIDER_TOKEN" "$(rd "$PATIENT_ID")"
step "reports update" PUT "/clinic/reports/update?id=$PATIENT_ID" "$PROVIDER_TOKEN" "$(body reports/update)"

section "Clinic: visit and its requests, prescriptions, paid services"
step "visits create" POST "/clinic/visits/create" "$PROVIDER_TOKEN" "$(body visits/create)"
setvar VISIT_ID '.id'  # Remember the new visit's id.
step "visits read" POST "/clinic/visits/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "visits update" PUT "/clinic/visits/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body visits/update)"

step "requests create" POST "/clinic/requests/create" "$PROVIDER_TOKEN" "$(body requests/create)"
setvar REQUEST_ID '.id'  # Remember the new request's id.
step "requests read" POST "/clinic/requests/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "requests update" PUT "/clinic/requests/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body requests/update)"

step "prescriptions create" POST "/clinic/prescriptions/create" "$PROVIDER_TOKEN" "$(body prescriptions/create)"
setvar PRESCRIPTION_ID '.id'  # Remember the new prescription's id.
step "prescriptions read" POST "/clinic/prescriptions/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "prescriptions update" PUT "/clinic/prescriptions/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body prescriptions/update)"

step "paidservices create" POST "/clinic/paidservices/create" "$PROVIDER_TOKEN" "$(body paidservices/create)"
setvar PAIDSERVICE_ID '.id'  # Remember the new paid-service record's id.
step "paidservices read" POST "/clinic/paidservices/read" "$PROVIDER_TOKEN" "$(rd "$VISIT_ID")"
step "paidservices update" PUT "/clinic/paidservices/update?id=$VISIT_ID" "$PROVIDER_TOKEN" "$(body paidservices/update)"

section "Appointments (needs clinic, user and provider)"
step "appointments create" POST "/appointments/clinics/create" "$PROVIDER_TOKEN" "$(body appointments/create)"
setvar APPOINTMENT_ID '.id'  # Remember the new appointment's id.
step "appointments read" POST "/appointments/clinics/read?id=$APPOINTMENT_ID" "$PROVIDER_TOKEN" "$(body appointments/read)"
step "appointments update" PUT "/appointments/clinics/update?id=$APPOINTMENT_ID" "$PROVIDER_TOKEN" "$(body appointments/update)"

# Suspend locks an account out of every endpoint (even activate and delete), so it cannot be tested on the
# accounts used above. It runs last on two disposable accounts, which stay suspended afterwards.
if [[ ${SKIP_SUSPEND:-0} != 1 ]]; then
	section "Suspend (disposable accounts, left suspended)"
	for group in providers users; do  # Test suspension on two disposable accounts.
		DISPOSABLE_NAME="${group%s}x${RUN}"  # Build a unique name for this disposable account.
		step "$group create (disposable)" POST "/clients/$group/create" "" "$(body "$group/disposable")"
		setvar sid '.id'  # Capture the disposable account's id.
		login "$group" "$DISPOSABLE_NAME" "$USER_PASSWORD"  # Log in with the fixture password.
		setvar stok '.token'  # Capture the disposable account's token.
		step "$group activate" POST "/clients/$group/activate?id=$sid" "$stok"
		step "$group suspend" POST "/clients/$group/suspend?id=$sid" "$stok"
		step "$group blocked while suspended" POST "/clients/$group/read" "$stok" "$(rd "$sid")" 401
	done
fi

# Teardown (deletes, logout, re-login, final deletes) and the summary run from the EXIT trap,
# so created data is cleaned up even when a step aborts the run.
