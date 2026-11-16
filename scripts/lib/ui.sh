#!/usr/bin/env bash
# Shared styling helpers for scripts.
# shellcheck disable=SC2034
if [[ -t 1 && "${VALHALLA_NO_COLOR:-0}" != "1" ]]; then
    C_R=$'\033[31m'; C_G=$'\033[32m'; C_Y=$'\033[33m'; C_B=$'\033[34m'
    C_M=$'\033[35m'; C_C=$'\033[36m'; C_DIM=$'\033[2m'; C_BOLD=$'\033[1m'
    C_N=$'\033[0m'
else
    C_R=""; C_G=""; C_Y=""; C_B=""; C_M=""; C_C=""; C_DIM=""; C_BOLD=""; C_N=""
fi

if [[ "${VALHALLA_NO_EMOJI:-0}" != "1" ]]; then
    E_ROCKET="🚀"; E_TEST="🧪"; E_LINT="🧹"; E_BUILD="🔨"; E_DB="🗄️"
    E_SERVER="🖥️"; E_STOP="🛑"; E_OK="✅"; E_FAIL="❌"; E_WARN="⚠️"
    E_INFO="ℹ️"; E_RUN="▶️"
else
    E_ROCKET=""; E_TEST=""; E_LINT=""; E_BUILD=""; E_DB=""
    E_SERVER=""; E_STOP=""; E_OK=""; E_FAIL=""; E_WARN=""
    E_INFO=""; E_RUN=""
fi

info()  { printf '%s%s%s %s\n' "$C_B" "$E_INFO" "$C_N" "$*"; }
ok()    { printf '%s%s%s %s\n' "$C_G" "$E_OK" "$C_N" "$*"; }
warn()  { printf '%s%s%s %s\n' "$C_Y" "$E_WARN" "$C_N" "$*" >&2; }
err()   { printf '%s%s%s %s\n' "$C_R" "$E_FAIL" "$C_N" "$*" >&2; }
step()  { printf '\n%s%s %s%s\n' "$C_BOLD$C_B" "$E_RUN" "$*" "$C_N"; }
dim()   { printf '%s%s%s\n' "$C_DIM" "$*" "$C_N"; }
