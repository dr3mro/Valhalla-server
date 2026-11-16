#!/usr/bin/env bash
# /**
#  * @file ui.sh
#  * @brief Shared color/style variables and message-printing helpers sourced by the project's shell scripts.
#  * @details Define the color and emoji variables plus small print helpers (info, ok, warn, err, step, dim) that other scripts reuse.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * run.sh sources this file so every command prints messages in a consistent style.
#  * It also lets users disable color, which matters for logs and CI output.
#  * Without it, each script would have to duplicate the same printing code.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash source builtin: https://www.gnu.org/software/bash/manual/bash.html
#  * - ANSI escape codes: https://en.wikipedia.org/wiki/ANSI_escape_code
#  * - ShellCheck: https://www.shellcheck.net/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Shared styling helpers for scripts.
# These variables are consumed by scripts that source this file, so ShellCheck's unused-variable warning is a false positive.
# Enable color only for an interactive terminal or when forced, honoring the NO_COLOR and VALHALLA_NO_COLOR opt-outs.
# shellcheck disable=SC2034
if [[ ( -t 1 || "${FORCE_COLOR:-0}" == "1" ) && "${VALHALLA_NO_COLOR:-0}" != "1" && -z "${NO_COLOR:-}" ]]; then  # -t 1 checks whether stdout is a terminal; FORCE_COLOR=1 can override that.
    # Pastel theme (256-color)
    C_R=$'\033[38;5;210m'; C_G=$'\033[38;5;150m'; C_Y=$'\033[38;5;222m'; C_B=$'\033[38;5;153m'  # The $'\033[...m' text is an ANSI escape code that switches the terminal color.
    C_M=$'\033[38;5;218m'; C_C=$'\033[38;5;159m'; C_DIM=$'\033[38;5;245m'; C_BOLD=$'\033[1m'  # C_N is the reset code that returns the terminal to its normal color.
    C_N=$'\033[0m'
else
    # Empty strings keep the printf format strings below valid without branching on color support.
    C_R=""; C_G=""; C_Y=""; C_B=""; C_M=""; C_C=""; C_DIM=""; C_BOLD=""; C_N=""  # Empty strings mean 'print nothing' when colors are turned off.
fi

# Emojis disabled; keep color-only output.
# The E_* variables remain as empty placeholders so callers can reference them unconditionally.
E_ROCKET=""; E_TEST=""; E_LINT=""; E_BUILD=""; E_DB=""  # These E_* variables are emoji placeholders that are intentionally left empty.
E_SERVER=""; E_STOP=""; E_OK=""; E_FAIL=""; E_WARN=""
E_INFO=""; E_RUN=""

# @brief Print an informational message using the info color slot.
# @param $* message text (all arguments joined).
# @return printf's exit status.
info()  { printf '%s%s%s %s\n' "$C_B" "$E_INFO" "$C_N" "$*"; }  # printf's %s slots are color, emoji, reset, then the message text.
# @brief Print a success message using the success color slot.
# @param $* message text.
# @return printf's exit status.
ok()    { printf '%s%s%s %s\n' "$C_G" "$E_OK" "$C_N" "$*"; }
# @brief Print a warning message to stderr using the warning color slot.
# @param $* message text.
# @return printf's exit status.
warn()  { printf '%s%s%s %s\n' "$C_Y" "$E_WARN" "$C_N" "$*" >&2; }  # >&2 sends warnings to stderr so they are not confused with normal results.
# @brief Print an error message to stderr using the error color slot.
# @param $* message text.
# @return printf's exit status.
err()   { printf '%s%s%s %s\n' "$C_R" "$E_FAIL" "$C_N" "$*" >&2; }  # >&2 sends errors to stderr as well.
# @brief Print a section heading with a leading blank line and bold styling.
# @param $* heading text.
# @return printf's exit status.
step()  { printf '\n%s%s %s%s\n' "$C_BOLD$C_B" "$E_RUN" "$*" "$C_N"; }  # The leading \n in the format adds a blank line before a section heading.
# @brief Print de-emphasized (dimmed) text.
# @param $* message text.
# @return printf's exit status.
dim()   { printf '%s%s%s\n' "$C_DIM" "$*" "$C_N"; }  # dim() is meant for less important text that should not grab attention.
