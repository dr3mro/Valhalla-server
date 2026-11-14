#!/usr/bin/env bash
# Run the Valhalla server image locally with Docker or Apple's `container`.
#
#   valhalla [options] <command> [args]      (installed)
#   ./valhalla.sh [options] <command> [args] (from a checkout)
#
# Container:
#   run [tag]        Start the container. Creates it (pulling the image if it's
#                    missing) if it doesn't exist; starts it if it's stopped.
#   update [tag]     Pull the image and recreate the container with the same
#                    settings (re-reading .env). Keeps the old container's
#                    TOKEN_SECRET unless .env or the shell sets one.
#   build [tag]      Build the image from this checkout (version from
#                    `git describe`) as valhalla-server-run:<tag> (default
#                    local), then recreate the container from it.
#   stop             Stop the container without removing it (run starts it).
#   restart          Restart the container.
#   delete           Remove the container (the image stays).
#   clean            Remove the container and every local image of the repo.
# Inspect:
#   status           Show state, image, port, health and version.
#   outdated         Compare the container's image with Docker Hub.
#   show             Follow the container's log (Ctrl+C stops following; the
#                    container keeps running).
#   logs [N]         Print the last N log lines (default 100).
#   stats            Live CPU and memory use.
#   env              Print the variables a new container would get (secrets
#                    hidden), without changing anything.
#   doc              Open the API docs in the browser (fails if not running).
# Database (reset on every start unless persistence is on, see --persist):
#   psql             Open psql on the database inside the container.
#   dump [file]      Save the database (default dumps/<name>-<time>.sql).
#   restore <file>   Load a dump into the running container.
# Other:
#   shell            Open a shell inside the container.
#   test [api|fuzz|all]
#                    Run test_api.sh (api, default) and/or
#                    schemathesis_test.sh (fuzz) against the container.
#   secret           Generate a random TOKEN_SECRET and write it to the env
#                    file (asks before replacing an existing one).
#   completion [zsh|bash]
#                    Print the shell completion script.
#   help             Show this help.
# Tool (both run install.sh):
#   install [ref]    Install or update the `valhalla` command (~/.local/bin,
#                    files in ~/.local/share/valhalla; an installed copy
#                    updates itself in place): downloads the latest installer, uninstalls the
#                    current copy, then installs. ref: branch, tag or commit
#                    (default main).
#   uninstall        Remove the installed `valhalla` command and its lines in
#                    ~/.zshrc / ~/.bashrc (asks first; keeps .env, dumps/).
#                    Uses the installer copy kept in the install directory,
#                    so it works offline; downloads it only if missing.
#
# tag defaults to "latest" (build: "local").
#
# Options (before or after the command):
#   --name NAME          container name     (env VALHALLA_NAME, default valhalla)
#   --port PORT          host port          (env VALHALLA_PORT, default 8080)
#   --runtime RT         docker | container (env VALHALLA_RUNTIME)
#   --env-file FILE      env file, or "none" (env VALHALLA_ENV_FILE)
#   -n, --dry-run        show what run/update/build/restore/secret/install/
#                        uninstall would do
#   -y, --yes            don't ask for confirmation
#   -p, --persist        keep the database across stop/restart/update/build/
#                        delete (see Persistence below); stored in the
#                        container, so later commands remember it
#   --no-persist         turn persistence off (with update/build: recreate
#                        without it)
#   -h, --help           show this help
#
# Other settings (environment variables):
#   VALHALLA_IMAGE       image repo (default docker.io/dr3mro/valhalla-server-run)
#   VALHALLA_CPUS        CPUs (default 4)
#   VALHALLA_MEMORY      memory, GB (default 1)
#   VALHALLA_DEPS_IMAGE  build: deps image, or "local" to build the deps from
#                        source (default: <repo>-deps:<arch> from Docker Hub)
#   VALHALLA_BUILD_MEMORY  build: Apple builder memory, GB (default 8)
#   VALHALLA_NO_COMPLETION=1  don't add shell completion to ~/.zshrc/~/.bashrc
#   VALHALLA_PERSIST=1   same as --persist when a container is created (also
#                        works in the env file)
#
# Persistence: the bundled database starts empty on every container start.
# With persistence on, the database is dumped to dumps/<name>-persist.sql
# before stop, restart, update, build and delete, and restored after run,
# restart, update and build (the previous dump is kept as .prev). If saving
# fails, nothing is stopped. Turning it on (run/update --persist) restores an
# existing saved file, or saves the current data if there's none yet. Note:
# a dump from an older image is restored as is, over a newer schema.
#
# Runtime: Apple `container` if installed, otherwise docker.
#
# Dependencies: before running, each command checks the tools and files it
# needs (e.g. curl, python3 for Apple container, jq for test, docker/Dockerfile
# for build) and lists every missing one with how to install it.
#
# Shell completion: on first use the script adds one line to ~/.zshrc and/or
# ~/.bashrc (whichever exist) to enable tab completion; open a new terminal to
# use it. Remove the line marked "valhalla completion" to undo.
#
# Server variables: every KEY=VALUE in the env file (default: .env in the
# current directory if there is one, otherwise .env next to this script) is
# passed to the container (comments and blank lines skipped,
# spaces around "=" and surrounding quotes removed). Precedence, lowest to
# highest:
#   built-in defaults (MAX_REQUESTS=1000000, MAX_FPS=3000000)
#   < env file
#   < MAX_REQUESTS / MAX_FPS / TOKEN_SECRET / SERVER_THREADS / DB_MAX_CONN
#     set in the shell (e.g. SERVER_THREADS=4 ./valhalla.sh update).
# Variables set nowhere aren't passed, so the server uses its own defaults.
# Without any TOKEN_SECRET, update keeps the old container's and a new
# container lets the image generate one. SERVER_PORT from the env file is the
# port published inside the container (default 8080). DB_HOST and DB_SSL are
# always overridden by the image, which runs its own Postgres.
#
# Server variables the image understands (server default in brackets):
#   Rate limiting / DoS
#     MAX_REQUESTS          max requests per IP per PERIOD        [10000]
#     PERIOD                rate-limit window, seconds            [30]
#     MAX_FPS               max fingerprints per IP per PERIOD    [100]
#     RL_DURATION           rate-limit duration, seconds          [30]
#     BAN_DURATION          ban duration, seconds                 [3600]
#     CLN_FRQ               cleanup interval, seconds             [30]
#     WHITELIST             comma-separated IP patterns           []
#     BLACKLIST             comma-separated IP patterns           []
#   Database (the image runs its own Postgres; DB_HOST/DB_SSL are ignored)
#     DB_PORT               [5432]      DB_NAME   [postgres]
#     DB_USER               [postgres]  DB_PASS   [postgres]
#     DB_MAX_CONN           connection pool size                  [10]
#   Server
#     SERVER_HOST           listen address (keep 0.0.0.0)         [0.0.0.0]
#     SERVER_PORT           listen port inside the container      [8080]
#     SERVER_THREADS        worker threads                        [4]
#     SERVER_NAME           [ProjectValhalla]
#     SERVER_DESCRIPTION    [ProjectValhalla API]
#     SERVER_DEBUG_LEVEL    [1]
#     SERVER_LOG_TO_CONSOLE [true]      SERVER_LOG_TO_FILE  [true]
#     SERVER_LOG_DIR        [./logs/]   SERVER_LOG_FILE     [server.log]
#     SERVER_UPLOAD_DIR     [./uploads/]
#   Tokens
#     TOKEN_SECRET          JWT signing secret (image generates one if unset)
#     TOKEN_VALIDITY        minutes                               [43200]
#     TOKEN_ISSUER          [ProjectValhalla]
#     TOKEN_TYPE            [JWS]
#   Frontend (invite links)
#     FRONTEND_HOST         [http://172.20.0.5]
#     FRONTEND_PORT         [3000]
#     FRONTEND_INVITE_PATH  [invite]
#   Email sender
#     EMAIL_SENDER_HOST     [http://172.20.0.6]
#     EMAIL_SENDER_PORT     [5000]
#     EMAIL_SENDER_QUEUE_PATH [/enqueue]
#
# Note: the database lives inside the container and is reset on every start.

set -eo pipefail

# Resolve symlinks (e.g. ~/bin/valhalla.sh -> repo) so ROOT is the repository.
SCRIPT="$0"
while [[ -L $SCRIPT ]]; do
  link="$(readlink "$SCRIPT")"
  [[ $link == /* ]] && SCRIPT="$link" || SCRIPT="$(dirname "$SCRIPT")/$link"
done
SCRIPT="$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")"
ROOT="$(dirname "$SCRIPT")"
# How the user called it (valhalla.sh, ./valhalla.sh, valhalla), for messages.
PROG="$0"
[[ $PROG == */* && $(command -v "$(basename "$PROG")" 2>/dev/null) == "$PROG" ]] && PROG="$(basename "$PROG")"

IMAGE_REPO="${VALHALLA_IMAGE:-docker.io/dr3mro/valhalla-server-run}"
LOCAL_REPO="valhalla-server-run"
NAME="${VALHALLA_NAME:-valhalla}"
PORT="${VALHALLA_PORT:-8080}"
CPUS="${VALHALLA_CPUS:-4}"
MEMORY_GB="${VALHALLA_MEMORY:-1}"
# Env file: --env-file / VALHALLA_ENV_FILE, else .env in the current
# directory, else .env next to this script.
if [[ -n ${VALHALLA_ENV_FILE:-} ]]; then
  ENV_FILE="$VALHALLA_ENV_FILE"
elif [[ -f $PWD/.env ]]; then
  ENV_FILE="$PWD/.env"
else
  ENV_FILE="$ROOT/.env"
fi
ENV_FILE_EXPLICIT="${VALHALLA_ENV_FILE:+1}"
DRY_RUN=0
ASSUME_YES=0
PERSIST=""     # --persist (1) / --no-persist (0); empty: keep the container's setting
OLD_PERSIST="" # VALHALLA_PERSIST of the container being replaced

# Commands: name|short description (single source for help-free tools such
# as shell completion).
COMMANDS=(
  "run|create or start the container"
  "update|pull the image and recreate the container"
  "build|build the image from this checkout and run it"
  "stop|stop the container without removing it"
  "restart|restart the container"
  "delete|remove the container"
  "clean|remove the container and its local images"
  "status|state, image, port, health and version"
  "outdated|compare the container's image with Docker Hub"
  "show|follow the container log"
  "logs|print the last N log lines"
  "stats|live CPU and memory use"
  "env|variables a new container would get"
  "doc|open the API docs in the browser"
  "psql|psql on the bundled database"
  "dump|save the database to a file"
  "restore|load a dump into the container"
  "shell|shell inside the container"
  "test|run the API tests against the container"
  "secret|write a random TOKEN_SECRET to the env file"
  "install|install or update the valhalla command from GitHub"
  "uninstall|remove the installed valhalla command"
  "completion|print the shell completion script"
  "help|show help"
)

# Options: names|description|value kind (empty: flag; value, runtime, file).
# Single source for shell completion; keep in sync with the parser below.
OPTIONS=(
  "--name|container name|value"
  "--port|host port|value"
  "--runtime|docker or container|runtime"
  "--env-file|env file, or none|file"
  "-n --dry-run|show what would happen|"
  "-y --yes|don't ask for confirmation|"
  "-p --persist|keep the database across stop/restart/update|"
  "--no-persist|turn persistence off|"
  "-h --help|show help|"
)

# Parsed env file (bash 3.2 has no associative arrays: parallel arrays).
ENV_KEYS=()
ENV_VALS=()

die() { echo "error: $*" >&2; exit 1; }
note() { echo "$*" >&2; }

usage() { sed -n '2,/^$/s/^# \{0,1\}//p' "$SCRIPT"; }

confirm() {
  [[ $ASSUME_YES == 1 ]] && return 0
  [[ -t 0 ]] || die "$1 (re-run with --yes to confirm)"
  local answer
  read -r -p "$1 [y/N] " answer
  [[ $answer == y || $answer == Y || $answer == yes ]]
}

# ── options ────────────────────────────────────────────────────────────────
ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --name) [[ $# -ge 2 ]] || die "--name needs a value"; NAME="$2"; shift 2 ;;
    --name=*) NAME="${1#*=}"; shift ;;
    --port) [[ $# -ge 2 ]] || die "--port needs a value"; PORT="$2"; shift 2 ;;
    --port=*) PORT="${1#*=}"; shift ;;
    --runtime) [[ $# -ge 2 ]] || die "--runtime needs a value"; VALHALLA_RUNTIME="$2"; shift 2 ;;
    --runtime=*) VALHALLA_RUNTIME="${1#*=}"; shift ;;
    --env-file) [[ $# -ge 2 ]] || die "--env-file needs a value"; ENV_FILE="$2"; ENV_FILE_EXPLICIT=1; shift 2 ;;
    --env-file=*) ENV_FILE="${1#*=}"; ENV_FILE_EXPLICIT=1; shift ;;
    -n | --dry-run) DRY_RUN=1; shift ;;
    -y | --yes) ASSUME_YES=1; shift ;;
    -p | --persist) PERSIST=1; shift ;;
    --no-persist) PERSIST=0; shift ;;
    -h | --help) ARGS=(help); break ;;
    --) shift; ARGS+=("$@"); break ;;
    -*) die "unknown option: $1 (see: $PROG help)" ;;
    *) ARGS+=("$1"); shift ;;
  esac
done
[[ $PORT =~ ^[0-9]+$ ]] || die "--port must be a number, got '$PORT'"
HEALTH_URL="http://127.0.0.1:${PORT}/api/v2/hello"
DOC_URL="http://127.0.0.1:${PORT}/api/v2/doc"

# ── shell completion ───────────────────────────────────────────────────────
command_names() {
  local entry
  for entry in "${COMMANDS[@]}"; do printf '%s ' "${entry%%|*}"; done
}

completion_zsh() {
  local entry
  cat <<'EOF'
# zsh completion for valhalla.sh (generated by: valhalla.sh completion zsh)
_valhalla() {
  local -a cmds
  cmds=(
EOF
  local desc q="'"
  for entry in "${COMMANDS[@]}"; do
    desc="${entry#*|}"
    desc="${desc//$q/$q\\$q$q}"
    printf "    '%s:%s'\n" "${entry%%|*}" "$desc"
  done
  cat <<'EOF'
  )
  local script=${words[1]} root
  [[ $script == */* ]] || script=${commands[$script]:-$script}
  root=${${script:A}:h}
  local -a opts optdesc
  opts=(
EOF
  local entry names desc kind spec n q="'"
  for entry in "${OPTIONS[@]}"; do
    names="${entry%%|*}"; desc="${entry#*|}"; kind="${desc#*|}"; desc="${desc%%|*}"
    desc="${desc//$q/$q\\$q$q}"
    case "$kind" in
      value) spec=":${names#--}:" ;;
      runtime) spec=":runtime:(docker container)" ;;
      file) spec=":file:_files" ;;
      *) spec="" ;;
    esac
    if [[ $names == *" "* ]]; then
      printf "    '(%s)'{%s}'[%s]'\n" "$names" "${names// /,}" "$desc"
    elif [[ -n $kind ]]; then
      printf "    '%s=[%s]%s'\n" "$names" "$desc" "$spec"
    else
      printf "    '%s[%s]'\n" "$names" "$desc"
    fi
  done
  echo "  )"
  echo "  optdesc=("
  for entry in "${OPTIONS[@]}"; do
    names="${entry%%|*}"; desc="${entry#*|}"; desc="${desc%%|*}"
    desc="${desc//$q/$q\\$q$q}"
    for n in $names; do printf "    '%s:%s'\n" "$n" "$desc"; done
  done
  echo "  )"
  cat <<'EOF'
  local curcontext=$curcontext state line
  _arguments -C -s $opts '1:command:->cmd' '*::arg:->args'
  case $state in
    cmd) _describe -t commands 'valhalla command' cmds ;;
    args)
      # Options are accepted after the command too.
      case ${words[CURRENT-1]} in
        --runtime) compadd docker container; return ;;
        --env-file) _files; return ;;
        --name|--port) _message 'value'; return ;;
      esac
      if [[ $PREFIX == -* ]]; then
        _describe -t options 'option' optdesc
        return
      fi
      case ${line[1]} in
        run|update|build|outdated|install)
          local -a tags
          tags=(latest local ${(f)"$(git -C $root tag --sort=-v:refname 2>/dev/null | head -n 20)"})
          _describe -t tags 'image tag' tags ;;
        restore) _files -g '*.sql(-.)' ;;
        dump) _files ;;
        logs) _message 'number of lines (default 100)' ;;
        test)
          local -a kinds
          kinds=('api:test_api.sh' 'fuzz:schemathesis_test.sh' 'all:both')
          _describe -t kinds 'test suite' kinds ;;
        completion) compadd zsh bash ;;
      esac ;;
  esac
}
EOF
  echo '(( $+functions[compdef] )) || { autoload -Uz compinit && compinit -i; }'
  printf 'compdef _valhalla valhalla valhalla.sh ./valhalla.sh %q\n' "$SCRIPT"
}

completion_bash() {
  cat <<'EOF'
# bash completion for valhalla.sh (generated by: valhalla.sh completion bash)
_valhalla() {
  local cur prev words cword
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
EOF
  local entry all=""
  for entry in "${OPTIONS[@]}"; do all+="${entry%%|*} "; done
  printf '  local opts="%s"\n' "${all% }"
  printf '  local cmds="%s"\n' "$(command_names)"
  cat <<'EOF'
  case "$prev" in
    --runtime) COMPREPLY=($(compgen -W "docker container" -- "$cur")); return ;;
    --env-file) COMPREPLY=($(compgen -f -- "$cur")); return ;;
    --name | --port) return ;;
  esac
  local i cmd="" root
  root="$(cd "$(dirname "$(command -v "${COMP_WORDS[0]}" 2>/dev/null || echo "${COMP_WORDS[0]}")")" 2>/dev/null && pwd)"
  for ((i = 1; i < COMP_CWORD; i++)); do
    case "${COMP_WORDS[i]}" in
      --name | --port | --runtime | --env-file) ((i++)) ;;
      -*) ;;
      *) cmd="${COMP_WORDS[i]}"; break ;;
    esac
  done
  if [[ $cur == -* ]]; then
    COMPREPLY=($(compgen -W "$opts" -- "$cur")); return
  fi
  case "$cmd" in
    "") COMPREPLY=($(compgen -W "$cmds" -- "$cur")) ;;
    run | update | build | outdated | install)
      COMPREPLY=($(compgen -W "latest local $(git -C "$root" tag 2>/dev/null)" -- "$cur")) ;;
    restore | dump) COMPREPLY=($(compgen -f -- "$cur")) ;;
    test) COMPREPLY=($(compgen -W "api fuzz all" -- "$cur")) ;;
    completion) COMPREPLY=($(compgen -W "zsh bash" -- "$cur")) ;;
  esac
}
EOF
  printf 'complete -F _valhalla valhalla valhalla.sh ./valhalla.sh %q\n' "$SCRIPT"
}

# install_completion: once, add an eval line to ~/.zshrc and ~/.bashrc
# (whichever exist). The marker makes it idempotent.
install_completion() {
  [[ ${VALHALLA_NO_COMPLETION:-0} == 1 ]] && return 0
  local shell rc tmp marker="# valhalla completion"
  for shell in zsh bash; do
    rc="$HOME/.${shell}rc"
    [[ -f $rc ]] || continue
    # Drop the line this tool added under its old name (deploy.sh).
    if grep -qF "# deploy.sh completion (added by" "$rc" 2>/dev/null; then
      tmp="$(mktemp)"
      awk '/^# deploy\.sh completion \(added by / { skip = 1; next } skip { skip = 0; next } { print }' "$rc" >"$tmp" &&
        cat "$tmp" >"$rc"
      rm -f "$tmp"
    fi
    grep -qF "$marker" "$rc" 2>/dev/null && continue
    # shellcheck disable=SC2016  # $(...) is meant to run in the rc file
    if printf '\n%s (added by %s; remove to undo)\n[ -x %q ] && eval "$(%q completion %s)"\n' \
      "$marker" "$SCRIPT" "$SCRIPT" "$SCRIPT" "$shell" >>"$rc" 2>/dev/null; then
      note "Added tab completion to $rc (open a new terminal to use it)."
    fi
  done
}

# ── dependency check ───────────────────────────────────────────────────────
MISSING=()
WARNINGS=()

# install_hint TOOL: how to get TOOL.
install_hint() {
  local brew=""
  command -v brew >/dev/null 2>&1 && brew="brew install"
  case "$1" in
    container) echo "https://github.com/apple/container/releases${brew:+ or: $brew container}" ;;
    docker) echo "https://docs.docker.com/get-docker/" ;;
    "container or docker") echo "Apple container: https://github.com/apple/container/releases; Docker: https://docs.docker.com/get-docker/" ;;
    python3) echo "${brew:+$brew python  (or: }xcode-select --install${brew:+)}" ;;
    git) echo "${brew:+$brew git  (or: }xcode-select --install${brew:+)}" ;;
    "open or xdg-open") echo "xdg-utils from your package manager" ;;
    "openssl or python3") echo "${brew:+$brew openssl or }install python3" ;;
    docker/Dockerfile) echo "build needs a full checkout: git clone https://github.com/dr3mro/Valhalla-server" ;;
    *.sh | */*) echo "missing next to $SCRIPT; reinstall, or restore it with: git checkout -- $1" ;;
    *) echo "${brew:-your package manager:} $1" ;;
  esac
}

need() {
  local t
  for t; do command -v "$t" >/dev/null 2>&1 || MISSING+=("$t"); done
}

# need_any LABEL TOOL...: at least one of TOOL.
need_any() {
  local label="$1" t
  shift
  for t; do command -v "$t" >/dev/null 2>&1 && return 0; done
  MISSING+=("$label")
}

# need_file PATH: a file in the repository.
need_file() { [[ -f $ROOT/$1 ]] || MISSING+=("$1"); }

# script_path NAME: NAME.sh next to this script (repository) or NAME
# (installed by install.sh, without the extension).
script_path() {
  if [[ -x $ROOT/$1.sh ]]; then echo "$ROOT/$1.sh"
  elif [[ -x $ROOT/$1 ]]; then echo "$ROOT/$1"
  else return 1
  fi
}

# need_script NAME: like need_file, for companion scripts.
need_script() { script_path "$1" >/dev/null || MISSING+=("$1.sh"); }

# want TOOL REASON: optional; only a warning.
want() { command -v "$1" >/dev/null 2>&1 || WARNINGS+=("$1 not found: $2"); }

# check_deps COMMAND [ARG]: lists every missing dependency of COMMAND at once
# and exits if a required one is missing.
check_deps() {
  local cmd="$1" arg="${2:-}"

  case "$cmd" in
    secret | test | install | uninstall) ;;
    *)
      if [[ -n ${VALHALLA_RUNTIME:-} ]]; then
        need "$VALHALLA_RUNTIME"
      elif [[ -z $RT ]]; then
        MISSING+=("container or docker")
      fi
      [[ $RT == container ]] && need python3
      ;;
  esac

  case "$cmd" in
    run | update | build | restart | status | doc) need curl ;;
    outdated) need curl python3 ;;
  esac

  case "$cmd" in
    build)
      need_file docker/Dockerfile
      want git "the image version will be \"dev\""
      ;;
    doc) need_any "open or xdg-open" open xdg-open ;;
    secret) need_any "openssl or python3" openssl python3 ;;
    install | uninstall) need curl ;;
    test)
      need curl
      case "${arg:-api}" in
        api | all) need jq; need_script test_api ;;
      esac
      case "${arg:-api}" in
        fuzz | all) need python3; need_script schemathesis_test ;;
      esac
      ;;
  esac

  local w
  for w in "${WARNINGS[@]}"; do note "warning: $w"; done
  [[ ${#MISSING[@]} -eq 0 ]] && return 0

  note "error: '$cmd' needs these, which are missing:"
  local m
  for m in "${MISSING[@]}"; do
    note "  - $m  ($(install_hint "$m"))"
  done
  exit 1
}

# ── runtime selection ──────────────────────────────────────────────────────
# resolve_runtime: sets RT (empty if none is installed); no side effects.
resolve_runtime() {
  RT="${VALHALLA_RUNTIME:-}"
  case "$RT" in
    "" | docker | container) ;;
    *) die "runtime must be 'docker' or 'container', got '$RT'" ;;
  esac
  if [[ -z $RT ]]; then
    if command -v container >/dev/null 2>&1; then
      RT=container
    elif command -v docker >/dev/null 2>&1; then
      RT=docker
    fi
  fi
}

# pick_runtime: RT is known and installed (check_deps ran); start Apple's
# container service if needed.
pick_runtime() {
  if [[ $RT == container ]] && ! container system status >/dev/null 2>&1; then
    note "Starting the container system service"
    container system start
  fi
}

# rt CMD...: runs `docker CMD...` or `container CMD...`.
rt() { if [[ $RT == docker ]]; then docker "$@"; else container "$@"; fi; }

# ── runtime-specific primitives ────────────────────────────────────────────
# apple_inspect PYTHON-EXPR: evaluates EXPR on the container's inspect JSON (d).
apple_inspect() {
  container inspect "$NAME" 2>/dev/null | EXPR="$1" python3 -c '
import json, os, sys
d = json.load(sys.stdin); d = d[0] if isinstance(d, list) else d
print(eval(os.environ["EXPR"]))' 2>/dev/null || true
}

exists() {
  if [[ $RT == docker ]]; then
    docker container inspect "$NAME" >/dev/null 2>&1
  else
    container inspect "$NAME" >/dev/null 2>&1
  fi
}

# state: prints "running" when running, anything else otherwise.
state() {
  if [[ $RT == docker ]]; then
    docker container inspect -f '{{.State.Status}}' "$NAME" 2>/dev/null || true
  else
    apple_inspect 'd.get("status", {}).get("state", "")'
  fi
}

is_running() { [[ "$(state)" == running ]]; }

require_exists() { exists || die "container $NAME doesn't exist; use: $PROG run"; }
require_running() {
  require_exists
  is_running || die "container $NAME is not running; use: $PROG run"
}

# container_env KEY: prints KEY's value in the existing container, if any.
container_env() {
  exists || return 0
  if [[ $RT == docker ]]; then
    docker container inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "$NAME" 2>/dev/null |
      sed -n "s/^$1=//p" | head -n1
  else
    KEY="$1" apple_inspect 'next((e[len(os.environ["KEY"]) + 1:] for e in d.get("configuration", {}).get("initProcess", {}).get("environment", []) if e.startswith(os.environ["KEY"] + "=")), "")'
  fi
}

old_secret() { container_env TOKEN_SECRET; }

# container_image: image reference the container was created from.
container_image() {
  if [[ $RT == docker ]]; then
    docker container inspect -f '{{.Config.Image}}' "$NAME" 2>/dev/null || true
  else
    apple_inspect 'd.get("configuration", {}).get("image", {}).get("reference", "")'
  fi
}

# container_digest: index digest (sha256:...) of the container's image.
container_digest() {
  if [[ $RT == docker ]]; then
    local id
    id="$(docker container inspect -f '{{.Image}}' "$NAME" 2>/dev/null)" || return 0
    docker image inspect -f '{{range .RepoDigests}}{{println .}}{{end}}' "$id" 2>/dev/null |
      sed -n 's/.*@//p' | head -n1
  else
    apple_inspect 'd.get("configuration", {}).get("image", {}).get("descriptor", {}).get("digest", "")'
  fi
}

# exec_it CMD...: runs CMD interactively in the container (replaces this shell).
exec_it() {
  local -a tty=(-i)
  [[ -t 0 && -t 1 ]] && tty=(-it)
  if [[ $RT == docker ]]; then
    exec docker exec "${tty[@]}" "$NAME" "$@"
  else
    exec container exec "${tty[@]}" "$NAME" "$@"
  fi
}

image_present() { rt image inspect "$1" >/dev/null 2>&1; }

pull() {
  note "Pulling $1 ($RT)"
  if [[ $RT == docker ]]; then docker pull "$1"; else container image pull "$1"; fi
}

remove_container() {
  if [[ $RT == docker ]]; then
    docker rm -f "$NAME" >/dev/null 2>&1 || true
  else
    container delete --force "$NAME" >/dev/null 2>&1 || true
  fi
}

start_container() { rt start "$NAME" >/dev/null; }
stop_container() { rt stop "$NAME" >/dev/null; }
logs() { rt logs "$NAME" 2>&1; }

# db_args: prints "user db port" of the bundled database.
db_settings() {
  local user db port
  user="$(container_env DB_USER)"
  db="$(container_env DB_NAME)"
  port="$(container_env DB_PORT)"
  echo "${user:-postgres} ${db:-postgres} ${port:-5432}"
}

# ── persistence ────────────────────────────────────────────────────────────
# With persistence on, the database is dumped to persist_file before the
# container stops or is replaced, and restored after it starts. The setting
# lives in the container as VALHALLA_PERSIST=1, so later commands remember it.
persist_file() { echo "$ROOT/dumps/${NAME}-persist.sql"; }

# persist_on: is persistence on for the existing container (or forced by flag)?
persist_on() {
  case "$PERSIST" in 1) return 0 ;; 0) return 1 ;; esac
  [[ "$(container_env VALHALLA_PERSIST)" == 1 ]]
}

# persist_wanted: should the container about to be created persist?
# --persist/--no-persist, else VALHALLA_PERSIST from the env file or shell,
# else the replaced container's setting.
persist_wanted() {
  case "$PERSIST" in 1) return 0 ;; 0) return 1 ;; esac
  local v
  v="$(env_get VALHALLA_PERSIST || true)"
  [[ -n ${VALHALLA_PERSIST:-} ]] && v="$VALHALLA_PERSIST"
  [[ -n $v ]] || v="$OLD_PERSIST"
  [[ $v == 1 ]]
}

# dump_to FILE: pg_dump of the bundled database into FILE (0 on success).
dump_to() {
  local file="$1" user db port
  read -r user db port <<<"$(db_settings)"
  mkdir -p "$(dirname "$file")"
  if rt exec "$NAME" su-exec postgres pg_dump -p "$port" -U "$user" -d "$db" \
    --clean --if-exists --no-owner --no-privileges >"$file" 2>/dev/null && [[ -s $file ]]; then
    return 0
  fi
  rm -f "$file"
  return 1
}

# restore_from FILE: loads FILE into the bundled database (0 on success).
restore_from() {
  local file="$1" user db port
  read -r user db port <<<"$(db_settings)"
  if [[ $RT == docker ]]; then
    docker exec -i "$NAME" su-exec postgres psql -q -v ON_ERROR_STOP=1 -p "$port" -U "$user" -d "$db" <"$file" >/dev/null
  else
    container exec -i "$NAME" su-exec postgres psql -q -v ON_ERROR_STOP=1 -p "$port" -U "$user" -d "$db" <"$file" >/dev/null
  fi
}

# persist_save: dumps the running database to persist_file (the previous
# dump is kept as .prev). Dies if the dump fails, before anything is stopped.
persist_save() {
  is_running || return 0
  local file
  file="$(persist_file)"
  [[ -f $file ]] && mv -f "$file" "$file.prev"
  note "Saving the database to $file"
  if ! dump_to "$file"; then
    [[ -f $file.prev ]] && mv -f "$file.prev" "$file"
    die "couldn't save the database, so nothing was stopped (use --no-persist to skip saving)"
  fi
}

# save_before_replace: should recreate/plan save the running database first?
# Yes if the old container persisted (unless --no-persist). When --persist
# turns it on for a container that didn't persist, only if nothing is saved
# yet: an existing saved file is what the new container restores.
save_before_replace() {
  [[ $PERSIST == 0 ]] && return 1
  [[ $OLD_PERSIST == 1 ]] && return 0
  [[ $PERSIST == 1 && ! -f "$(persist_file)" ]]
}

# persist_load: restores persist_file into the freshly started container.
persist_load() {
  local file
  file="$(persist_file)"
  if [[ ! -f $file ]]; then
    note "Persistence is on; no saved database yet ($file)"
    return 0
  fi
  note "Restoring the database from $file"
  restore_from "$file" || note "warning: restoring $file failed; the database is the image's fresh one"
}

# ── env file ───────────────────────────────────────────────────────────────
# load_env: parses ENV_FILE into ENV_KEYS/ENV_VALS. Later duplicates win.
load_env() {
  [[ $ENV_FILE == none ]] && return 0
  if [[ ! -f $ENV_FILE ]]; then
    [[ -n $ENV_FILE_EXPLICIT ]] && die "env file not found: $ENV_FILE"
    return 0
  fi

  local line key val
  while IFS= read -r line || [[ -n $line ]]; do
    line="${line%$'\r'}"
    [[ $line =~ ^[[:space:]]*(#|$) ]] && continue
    [[ $line == *=* ]] || continue
    key="${line%%=*}"
    val="${line#*=}"
    key="${key#"${key%%[![:space:]]*}"}"; key="${key%"${key##*[![:space:]]}"}"
    key="${key#export }"
    val="${val#"${val%%[![:space:]]*}"}"; val="${val%"${val##*[![:space:]]}"}"
    if [[ ${#val} -ge 2 && ( ( $val == \"*\" ) || ( $val == \'*\' ) ) ]]; then
      val="${val:1:${#val}-2}"
    fi
    [[ $key =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || { note "warning: skipping invalid key '$key' in $ENV_FILE"; continue; }
    env_set "$key" "$val"
  done <"$ENV_FILE"
  note "Env file: $ENV_FILE (${#ENV_KEYS[@]} variables)"
}

# env_get KEY: prints the value; exit 1 if the key isn't set.
env_get() {
  local i
  for i in "${!ENV_KEYS[@]}"; do
    if [[ ${ENV_KEYS[$i]} == "$1" ]]; then
      printf '%s' "${ENV_VALS[$i]}"
      return 0
    fi
  done
  return 1
}

# env_set KEY VALUE: adds or replaces a value.
env_set() {
  local i
  for i in "${!ENV_KEYS[@]}"; do
    if [[ ${ENV_KEYS[$i]} == "$1" ]]; then
      ENV_VALS[i]="$2"
      return 0
    fi
  done
  ENV_KEYS+=("$1")
  ENV_VALS+=("$2")
}

# compose_env [fallback TOKEN_SECRET]: applies defaults and shell overrides
# on top of the env file, giving the final container environment.
compose_env() {
  local fallback_secret="${1:-}"

  # Defaults < env file < shell.
  env_get MAX_REQUESTS >/dev/null || env_set MAX_REQUESTS 1000000
  env_get MAX_FPS >/dev/null || env_set MAX_FPS 3000000
  # Threads / pool size: from the env file if set there (or the shell);
  # otherwise not passed, so the server uses its own defaults.
  [[ -n ${SERVER_THREADS:-} ]] && env_set SERVER_THREADS "$SERVER_THREADS"
  [[ -n ${DB_MAX_CONN:-} ]] && env_set DB_MAX_CONN "$DB_MAX_CONN"
  [[ -n ${MAX_REQUESTS:-} ]] && env_set MAX_REQUESTS "$MAX_REQUESTS"
  [[ -n ${MAX_FPS:-} ]] && env_set MAX_FPS "$MAX_FPS"
  if persist_wanted; then
    env_set VALHALLA_PERSIST 1
  elif env_get VALHALLA_PERSIST >/dev/null || [[ -n $OLD_PERSIST ]]; then
    env_set VALHALLA_PERSIST 0
  fi
  if [[ -n ${TOKEN_SECRET:-} ]]; then
    env_set TOKEN_SECRET "$TOKEN_SECRET"
  elif ! env_get TOKEN_SECRET >/dev/null && [[ -n $fallback_secret ]]; then
    env_set TOKEN_SECRET "$fallback_secret"
  fi
  return 0
}

# print_env: the composed environment, sorted, secrets hidden.
print_env() {
  local i key
  for i in "${!ENV_KEYS[@]}"; do
    key="${ENV_KEYS[$i]}"
    if [[ $key =~ (PASS|SECRET|KEY) ]]; then
      echo "$key=<hidden>"
    else
      echo "$key=${ENV_VALS[$i]}"
    fi
  done | sort
}

# create_container <image> [fallback TOKEN_SECRET, e.g. the old container's]
create_container() {
  local image="$1" i
  compose_env "${2:-}"

  local server_port server_host
  server_port="$(env_get SERVER_PORT || echo 8080)"
  server_host="$(env_get SERVER_HOST || echo 0.0.0.0)"
  case "$server_host" in
    127.* | localhost | ::1)
      note "warning: SERVER_HOST=$server_host only listens inside the container; the published port won't reach it. Use 0.0.0.0." ;;
  esac

  local -a args=(-d --name "$NAME" -p "${PORT}:${server_port}")
  for i in "${!ENV_KEYS[@]}"; do
    args+=(-e "${ENV_KEYS[$i]}=${ENV_VALS[$i]}")
  done

  if [[ $RT == docker ]]; then
    args+=(--cpus "$CPUS" --memory "${MEMORY_GB}g")
  else
    args+=(-c "$CPUS" -m "${MEMORY_GB}G")
  fi
  rt run "${args[@]}" "$image" >/dev/null
}

# plan_recreate <image> <action>: dry-run output for run/update/build.
plan_recreate() {
  OLD_PERSIST="$(container_env VALHALLA_PERSIST)"
  compose_env "$(old_secret)"
  echo "Dry run ($RT), nothing changed. Would:"
  echo "  $2"
  if exists && is_running && save_before_replace; then
    echo "  save the database to $(persist_file)"
  fi
  exists && echo "  remove container $NAME"
  echo "  create $NAME from $1: port ${PORT}->$(env_get SERVER_PORT || echo 8080), ${CPUS} CPUs, ${MEMORY_GB} GB"
  persist_wanted && echo "  restore the database from $(persist_file) (persistence on)"
  echo "  with these variables:"
  print_env | sed 's/^/    /'
}

# ── helpers ────────────────────────────────────────────────────────────────
wait_healthy() {
  printf "Waiting for the server" >&2
  for _ in $(seq 1 30); do
    if curl -fsS "$HEALTH_URL" >/dev/null 2>&1; then
      note " up at $HEALTH_URL"
      logs | grep -i -m1 -E "version" || true
      return 0
    fi
    printf "." >&2
    sleep 2
  done
  note " not answering after 60s. Recent logs:"
  logs | tail -n 20
  return 1
}

image_ref() { echo "${IMAGE_REPO}:${1:-latest}"; }

host_arch() {
  case "$(uname -m)" in
    arm64 | aarch64) echo arm64 ;;
    x86_64 | amd64) echo amd64 ;;
    *) uname -m ;;
  esac
}

# registry_digest <repo> <tag>: index digest on Docker Hub (anonymous).
registry_digest() {
  local repo="${1#docker.io/}" tag="$2" token
  [[ $repo == */* ]] || repo="library/$repo"
  token="$(curl -fsS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:${repo}:pull" |
    python3 -c 'import json, sys; print(json.load(sys.stdin)["token"])')" || return 1
  curl -fsSI \
    -H "Authorization: Bearer $token" \
    -H "Accept: application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.v2+json" \
    "https://registry-1.docker.io/v2/${repo}/manifests/${tag}" |
    tr -d '\r' | sed -n 's/^[Dd]ocker-[Cc]ontent-[Dd]igest: //p'
}

# ensure_builder: Apple's default builder (2 GB) runs out of memory compiling
# the server; give it VALHALLA_BUILD_MEMORY GB.
ensure_builder() {
  [[ $RT == container ]] || return 0
  local want_gb="${VALHALLA_BUILD_MEMORY:-8}" have cpus
  have="$(container builder status --format json 2>/dev/null | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin); d = d[0] if isinstance(d, list) else d
    print(d["configuration"]["resources"]["memoryInBytes"] // (1 << 30))
except Exception:
    print(0)' 2>/dev/null || echo 0)"
  [[ $have -ge $want_gb ]] && return 0
  cpus="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  note "Restarting the Apple builder with ${want_gb} GB and ${cpus} CPUs (had ${have} GB)"
  container builder stop >/dev/null 2>&1 || true
  container builder start --cpus "$cpus" --memory "${want_gb}g" >/dev/null
}

# ── commands ───────────────────────────────────────────────────────────────
cmd_run() {
  local image
  image="$(image_ref "${1:-}")"
  if exists; then
    if [[ -n ${1:-} && "$(container_image)" != "$image" ]]; then
      note "note: $NAME already exists from $(container_image); $1 is ignored (use: $PROG update $1)"
    fi
    if is_running; then
      echo "$NAME is already running."
      wait_healthy
      return
    fi
    if [[ $DRY_RUN == 1 ]]; then
      echo "Dry run ($RT): would start the existing container $NAME."
      return
    fi
    note "Starting existing container $NAME"
    start_container
    wait_healthy
    persist_on && persist_load
    return 0
  else
    if [[ $DRY_RUN == 1 ]]; then
      local action="use local image $image"
      image_present "$image" || action="pull $image"
      plan_recreate "$image" "$action"
      return
    fi
    image_present "$image" || pull "$image"
    note "Creating container $NAME from $image"
    create_container "$image"
  fi
  wait_healthy
  persist_on && persist_load
  return 0
}

# recreate <image>: replaces the container with one from <image>, keeping its
# TOKEN_SECRET and, with persistence, its data.
recreate() {
  local image="$1" secret
  secret="$(old_secret)"
  OLD_PERSIST="$(container_env VALHALLA_PERSIST)"
  if exists && save_before_replace; then
    persist_save
  elif exists && is_running && [[ $PERSIST == 1 && $OLD_PERSIST != 1 ]]; then
    note "Turning persistence on: the new container gets the saved database $(persist_file);"
    note "the current one isn't saved (run '$PROG dump' first to keep it)"
  fi
  note "Recreating container $NAME from $image"
  remove_container
  create_container "$image" "$secret"
  wait_healthy
  persist_on && persist_load
  return 0
}

cmd_update() {
  local image
  image="$(image_ref "${1:-}")"
  if [[ $DRY_RUN == 1 ]]; then
    plan_recreate "$image" "pull $image"
    return
  fi
  pull "$image"
  recreate "$image"
}

cmd_build() {
  local tag="${1:-local}" version deps image
  image="${LOCAL_REPO}:${tag}"
  version="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev)"
  deps="${VALHALLA_DEPS_IMAGE:-${IMAGE_REPO%-run}-deps:$(host_arch)}"

  local -a args=(build -f "$ROOT/docker/Dockerfile" --build-arg "VALHALLA_VERSION=$version" -t "$image")
  [[ $deps != local ]] && args+=(--build-arg "DEPS_IMAGE=$deps")
  args+=("$ROOT")

  if [[ $DRY_RUN == 1 ]]; then
    echo "Dry run ($RT), nothing changed. Would build:"
    echo "  $RT ${args[*]}"
    [[ $deps == local ]] && echo "  (deps built from source: slow)"
    plan_recreate "$image" "then run the new image"
    return
  fi

  ensure_builder
  note "Building $image (version $version, deps: $deps)"
  rt "${args[@]}"
  recreate "$image"
}

cmd_stop() {
  require_exists
  if is_running; then
    persist_on && persist_save
    stop_container
    echo "Stopped $NAME (start it again with: $PROG run)"
  else
    echo "$NAME is not running."
  fi
}

cmd_restart() {
  require_exists
  if persist_on; then
    note "Restarting $NAME (persistence on: the database is saved and restored)"
    persist_save
  else
    note "Restarting $NAME (the database is reset)"
  fi
  is_running && stop_container
  start_container
  wait_healthy
  persist_on && persist_load
  return 0
}

cmd_delete() {
  if exists; then
    if persist_on && is_running; then
      persist_save
      note "Kept $(persist_file); $PROG run --persist restores it"
    fi
    remove_container
    echo "Removed container $NAME"
  else
    echo "Container $NAME doesn't exist."
  fi
}

cmd_clean() {
  cmd_delete
  local refs pattern
  pattern="^((docker\.io/)?${IMAGE_REPO#docker.io/}|${LOCAL_REPO}):"
  if [[ $RT == docker ]]; then
    refs="$(docker image ls --format '{{.Repository}}:{{.Tag}}' | grep -E "$pattern" || true)"
    [[ -n $refs ]] && echo "$refs" | xargs docker image rm >/dev/null
  else
    refs="$(container image list 2>/dev/null | awk 'NR>1 {print $1":"$2}' | grep -E "$pattern" || true)"
    [[ -n $refs ]] && echo "$refs" | xargs container image delete >/dev/null
  fi
  if [[ -n $refs ]]; then
    echo "Removed images:"
    while IFS= read -r ref; do echo "  $ref"; done <<<"$refs"
  else
    echo "No local images of $IMAGE_REPO."
  fi
}

cmd_status() {
  echo "Runtime:   $RT"
  echo "Container: $NAME"
  if ! exists; then
    echo "State:     not created (use: $PROG run)"
    return 0
  fi
  local st
  st="$(state)"
  echo "State:     ${st:-unknown}"
  echo "Image:     $(container_image)"
  echo "URL:       http://127.0.0.1:${PORT}  (docs: $DOC_URL)"
  local pf
  pf="$(persist_file)"
  if persist_on; then
    if [[ -f $pf ]]; then
      echo "Persist:   on, last saved $(date -r "$pf" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?') ($pf)"
    else
      echo "Persist:   on, nothing saved yet ($pf)"
    fi
  else
    echo "Persist:   off (the database is reset on every start; enable: $PROG update --persist)"
  fi
  if [[ $st == running ]]; then
    if curl -fsS "$HEALTH_URL" >/dev/null 2>&1; then
      echo "Health:    answering at $HEALTH_URL"
    else
      echo "Health:    not answering at $HEALTH_URL"
    fi
    local ver
    ver="$(logs | grep -i -m1 -E "version" | sed -E $'s/\x1b\\[[0-9;]*m//g; s/.*[Vv]ersion[^:]*:[[:space:]]*//')"
    echo "Version:   ${ver:-unknown}"
  fi
}

cmd_outdated() {
  require_exists
  local ref repo tag local_digest remote_digest
  ref="$(container_image)"
  repo="${ref%:*}"
  tag="${1:-${ref##*:}}"
  [[ $repo == "$LOCAL_REPO" || $repo == "docker.io/library/$LOCAL_REPO" ]] &&
    die "$NAME runs a locally built image ($ref); nothing to compare on Docker Hub"
  [[ $repo == docker.io/* || $repo != *.*/* ]] || die "only Docker Hub images can be checked ($ref)"
  local_digest="$(container_digest)"
  remote_digest="$(registry_digest "$repo" "$tag")" || die "couldn't reach Docker Hub"
  [[ -n $remote_digest ]] || die "tag $tag not found for $repo"
  echo "Container: $ref"
  echo "  running   ${local_digest:-unknown}"
  echo "  Docker Hub ${tag}: $remote_digest"
  if [[ $local_digest == "$remote_digest" ]]; then
    echo "Up to date."
  else
    echo "Update available: $PROG update $tag"
  fi
}

cmd_show() {
  require_exists
  if [[ $RT == docker ]]; then exec docker logs -f "$NAME"; else exec container logs -f "$NAME"; fi
}

cmd_logs() {
  require_exists
  local n="${1:-100}"
  [[ $n =~ ^[0-9]+$ ]] || die "logs takes a number of lines, got '$n'"
  if [[ $RT == docker ]]; then docker logs --tail "$n" "$NAME" 2>&1; else container logs -n "$n" "$NAME" 2>&1; fi
}

cmd_stats() {
  require_running
  if [[ $RT == docker ]]; then exec docker stats "$NAME"; else exec container stats "$NAME"; fi
}

cmd_env() {
  compose_env "$(old_secret)"
  print_env
}

cmd_doc() {
  require_running
  curl -fsS "$HEALTH_URL" >/dev/null 2>&1 ||
    die "$NAME is running but the server isn't answering at $HEALTH_URL (see: $PROG logs)"
  echo "Opening $DOC_URL"
  if command -v open >/dev/null 2>&1; then
    open "$DOC_URL"
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$DOC_URL" >/dev/null 2>&1
  else
    die "no 'open' or 'xdg-open' found; browse to $DOC_URL"
  fi
}

cmd_psql() {
  require_running
  local user db port
  read -r user db port <<<"$(db_settings)"
  exec_it su-exec postgres psql -p "$port" -U "$user" -d "$db"
}

cmd_dump() {
  require_running
  local file="${1:-$ROOT/dumps/${NAME}-$(date +%Y%m%d-%H%M%S).sql}"
  note "Dumping the database from $NAME"
  dump_to "$file" || die "pg_dump failed"
  echo "Saved $(wc -c <"$file" | tr -d ' ') bytes to $file"
}

cmd_restore() {
  local file="${1:-}"
  [[ -n $file ]] || die "usage: $PROG restore <file>"
  [[ -f $file ]] || die "file not found: $file"
  require_running
  local user db port
  read -r user db port <<<"$(db_settings)"
  if [[ $DRY_RUN == 1 ]]; then
    echo "Dry run ($RT): would load $file into database $db in $NAME, replacing its contents."
    return
  fi
  confirm "Replace the contents of database $db in $NAME with $file?" || die "cancelled"
  note "Restoring $file into $db"
  restore_from "$file" || die "restore failed"
  echo "Restored $file"
}

cmd_shell() {
  require_running
  exec_it sh
}

cmd_test() {
  local kind="${1:-api}" base="http://127.0.0.1:${PORT}/api/v2" status=0
  case "$kind" in api | fuzz | all) ;; *) die "test takes api, fuzz or all, got '$kind'" ;; esac
  curl -fsS "$HEALTH_URL" >/dev/null 2>&1 || die "the server isn't answering at $HEALTH_URL; use: $PROG run"
  if [[ $kind == api || $kind == all ]]; then
    note "Running test_api.sh against $base"
    BASE_URL="$base" "$(script_path test_api)" || status=$?
  fi
  if [[ $kind == fuzz || $kind == all ]]; then
    note "Running schemathesis_test.sh against $base"
    BASE_URL="$base" "$(script_path schemathesis_test)" || status=$?
  fi
  return "$status"
}

cmd_secret() {
  [[ $ENV_FILE != none ]] || die "no env file (--env-file none)"
  local secret has=0
  [[ -f $ENV_FILE ]] && grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$ENV_FILE" && has=1
  if [[ $DRY_RUN == 1 ]]; then
    if [[ $has == 1 ]]; then
      echo "Dry run: would replace TOKEN_SECRET in $ENV_FILE with a new random value."
    else
      echo "Dry run: would add a random TOKEN_SECRET to $ENV_FILE."
    fi
    return
  fi
  if [[ $has == 1 ]]; then
    confirm "Replace the TOKEN_SECRET in $ENV_FILE? Tokens signed with the old one stop working." || die "cancelled"
  fi
  if command -v openssl >/dev/null 2>&1; then
    secret="$(openssl rand -hex 32)"
  else
    secret="$(python3 -c 'import secrets; print(secrets.token_hex(32))')"
  fi
  if [[ $has == 1 ]]; then
    local tmp
    tmp="$(mktemp "${ENV_FILE}.XXXXXX")"
    SECRET="$secret" awk '
      /^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=/ && !done { print "TOKEN_SECRET = \"" ENVIRON["SECRET"] "\""; done = 1; next }
      { print }' "$ENV_FILE" >"$tmp"
    chmod "$(stat -f %Lp "$ENV_FILE" 2>/dev/null || stat -c %a "$ENV_FILE")" "$tmp" 2>/dev/null || true
    mv "$tmp" "$ENV_FILE"
    echo "Replaced TOKEN_SECRET in $ENV_FILE. Apply it with: $PROG update"
  else
    [[ -f $ENV_FILE ]] || (umask 077 && : >"$ENV_FILE")
    printf '\nTOKEN_SECRET = "%s"\n' "$secret" >>"$ENV_FILE"
    echo "Added TOKEN_SECRET to $ENV_FILE. Apply it with: $PROG update"
  fi
}

INSTALLER_URL="${VALHALLA_INSTALLER_URL:-https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh}"

# local_installer: the installer kept by install.sh in the install directory
# (or this checkout's install.sh); prints nothing if there's none.
local_installer() {
  local f
  for f in "$ROOT/installer" "$HOME/.local/share/valhalla/installer" "$ROOT/install.sh"; do
    [[ -f $f ]] && { echo "$f"; return 0; }
  done
  return 0
}

# install_target: the install directory; --dir is passed for an installed copy (install.sh writes VERSION),
# so it acts on its own directory; from a checkout the installer's default
# (~/.local/share/valhalla) is used.
install_target() { if [[ -f $ROOT/VERSION ]]; then echo "$ROOT"; else echo "$HOME/.local/share/valhalla"; fi; }

# fetch_installer: downloads install.sh from GitHub into a temp file and
# prints its path.
fetch_installer() {
  local tmp
  tmp="$(mktemp)"
  if ! curl -fsSL "$INSTALLER_URL" -o "$tmp"; then
    rm -f "$tmp"
    die "couldn't download the installer from $INSTALLER_URL"
  fi
  echo "$tmp"
}

cmd_install() {
  local ref="${1:-main}" target new old=""
  target="$(install_target)"
  local -a dir_args=()
  [[ -f $ROOT/VERSION ]] && dir_args=(--dir "$ROOT")
  [[ -f $target/valhalla || -f $target/VERSION ]] && old="$(local_installer)"

  if [[ $DRY_RUN == 1 ]]; then
    echo "Dry run, nothing changed. Would:"
    echo "  download $INSTALLER_URL"
    if [[ -f $target/valhalla || -f $target/VERSION ]]; then
      echo "  uninstall the current copy in $target with ${old:-the downloaded installer} (keeps .env, dumps/)"
      [[ -f $target/VERSION ]] && echo "    installed now: $(cat "$target/VERSION")"
    fi
    echo "  install $ref into $target"
    return 0
  fi

  # Download first: if that fails, the current install is left alone.
  new="$(fetch_installer)"
  if [[ -f $target/valhalla || -f $target/VERSION ]]; then
    note "Removing the current install in $target first"
    sh "${old:-$new}" --uninstall "${dir_args[@]}" || { rm -f "$new"; die "uninstall failed"; }
  fi
  note "Installing $ref"
  sh "$new" --ref "$ref" "${dir_args[@]}" || { rm -f "$new"; die "the installer failed"; }
  rm -f "$new"
}

cmd_uninstall() {
  local target installer tmp=""
  target="$(install_target)"
  local -a dir_args=()
  [[ -f $ROOT/VERSION ]] && dir_args=(--dir "$ROOT")
  installer="$(local_installer)"

  if [[ $DRY_RUN == 1 ]]; then
    echo "Dry run: would run ${installer:-$INSTALLER_URL (downloaded)} with: --uninstall ${dir_args[*]}"
    return 0
  fi
  confirm "Remove the valhalla command from $target and its lines in ~/.zshrc / ~/.bashrc? (.env and dumps/ are kept)" ||
    die "cancelled"
  if [[ -z $installer ]]; then
    tmp="$(fetch_installer)"
    installer="$tmp"
  fi
  note "Running $installer (--uninstall ${dir_args[*]})"
  sh "$installer" --uninstall "${dir_args[@]}" || { [[ -n $tmp ]] && rm -f "$tmp"; die "the installer failed"; }
  [[ -n $tmp ]] && rm -f "$tmp"
  return 0
}

cmd_completion() {
  local shell="${1:-}"
  if [[ -z $shell ]]; then
    shell="$(basename "${SHELL:-zsh}")"
  fi
  case "$shell" in
    zsh) completion_zsh ;;
    bash) completion_bash ;;
    *) die "completion supports zsh or bash, got '$shell'" ;;
  esac
}

# ── main ───────────────────────────────────────────────────────────────────
command="${ARGS[0]:-help}"
ARGS=("${ARGS[@]:1}")

case "$command" in
  help) usage; exit 0 ;;
  completion) cmd_completion "${ARGS[@]}"; exit 0 ;;
esac

known=" $(command_names)"
[[ $known == *" $command "* ]] || { note "unknown command: $command (see: $PROG help)"; exit 1; }

install_completion

resolve_runtime
check_deps "$command" "${ARGS[0]:-}"

case "$command" in
  secret | test | install | uninstall) "cmd_${command}" "${ARGS[@]}"; exit $? ;;
esac

pick_runtime
case "$command" in run | update | build | env) load_env ;; esac
"cmd_${command}" "${ARGS[@]}"
