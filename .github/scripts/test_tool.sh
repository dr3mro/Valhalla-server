#!/usr/bin/env bash
# Offline tests for the shell scripts: install.sh, the vdt tool (valhalla.sh)
# and the helper scripts. Needs no container runtime and no network: a stub
# `docker` that always fails stands in for "Docker installed, daemon not
# running", so the container commands are tested through their dry runs and
# their "no container" errors. Runs on Linux and macOS (bash 3.2 too).
#
#   .github/scripts/test_tool.sh
#
# Env: TOOL_BASH  bash used to run the tool (default: bash on PATH; CI also
#                 runs it with macOS's /bin/bash 3.2)
#      POSIX_SH   sh used to run install.sh (default: dash if found, else sh)
#
# Needs: jq, zsh (completion), fzf (payload), python3, git.

# shellcheck disable=SC2016  # single-quoted code runs in a child shell
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOL_BASH="${TOOL_BASH:-bash}"
if [[ -z ${POSIX_SH:-} ]]; then
  if command -v dash >/dev/null 2>&1; then POSIX_SH="dash"; else POSIX_SH="sh"; fi
fi
OS="$(uname -s)"

PASS=0
FAIL=0
FAILED=()

# ── helpers ────────────────────────────────────────────────────────────────
section() { printf '\n== %s ==\n' "$1"; }

ok() { PASS=$((PASS + 1)); printf '  ok    %s\n' "$1"; }
bad() {
  FAIL=$((FAIL + 1)); FAILED+=("$1"); printf '  FAIL  %s\n' "$1"
  if [[ -n ${2:-} ]]; then printf '%s\n' "$2" | head -n 20 | sed 's/^/        | /'; fi
}

# check NAME CMD...: CMD succeeds.
check() {
  local name="$1" out; shift
  if out="$("$@" 2>&1)"; then ok "$name"; else bad "$name" "$out"; fi
}

# fails NAME PATTERN CMD...: CMD fails and its output matches PATTERN (ERE).
fails() {
  local name="$1" pat="$2" out; shift 2
  if out="$("$@" 2>&1)"; then bad "$name (expected failure)" "$out"
  elif printf '%s' "$out" | grep -qE -- "$pat"; then ok "$name"
  else bad "$name (output doesn't match /$pat/)" "$out"
  fi
}

# outputs NAME PATTERN CMD...: CMD succeeds and its output matches PATTERN.
outputs() {
  local name="$1" pat="$2" out; shift 2
  if ! out="$("$@" 2>&1)"; then bad "$name (exit != 0)" "$out"
  elif printf '%s' "$out" | grep -qE -- "$pat"; then ok "$name"
  else bad "$name (output doesn't match /$pat/)" "$out"
  fi
}

# Every test runs in a throwaway HOME, with a stub docker first on PATH.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STUB="$WORK/stub"
mkdir -p "$STUB"
cat >"$STUB/docker" <<'EOF'
#!/bin/sh
echo "Cannot connect to the Docker daemon at unix:///var/run/docker.sock. Is the docker daemon running?" >&2
exit 1
EOF
chmod +x "$STUB/docker"
ORIG_PATH="$PATH"
BASE_PATH="$STUB:$PATH"

# fresh_home SHELL: new empty HOME with SHELL as the login shell.
fresh_home() {
  export HOME="$WORK/home-$RANDOM$RANDOM" SHELL="$1"
  mkdir -p "$HOME"
  D="$HOME/.local/share/vdt"
  V="$HOME/.local/bin/vdt"
}

export PATH="$BASE_PATH"
export VALHALLA_SOURCE="$REPO" VALHALLA_NO_COMPLETION=1 VALHALLA_NO_UPDATE_CHECK=1
export VALHALLA_RUNTIME=docker TERM=dumb
unset VALHALLA_ENV_FILE VALHALLA_INSTALL_DIR VALHALLA_BIN_DIR VALHALLA_REF GITHUB_TOKEN NO_COLOR
cd "$WORK" || exit 1 # no .env in the current directory

vdt() { "$TOOL_BASH" "$V" "$@"; }
install_sh() { "$POSIX_SH" "$REPO/install.sh" "$@"; }

echo "OS: $OS  tool bash: $("$TOOL_BASH" -c 'echo $BASH_VERSION')  installer sh: $POSIX_SH"

# ── syntax ─────────────────────────────────────────────────────────────────
section "Syntax"
while IFS= read -r f; do
  case "$(head -n 1 "$REPO/$f")" in
    *bash*) check "bash -n $f" "$TOOL_BASH" -n "$REPO/$f" ;;
    *) check "$POSIX_SH -n $f" "$POSIX_SH" -n "$REPO/$f" ;;
  esac
done < <(git -C "$REPO" ls-files '*.sh')

# ── installer ──────────────────────────────────────────────────────────────
section "Installer: install (bash login shell)"
fresh_home /bin/bash
printf 'export KEEP_ME=1\n' >"$HOME/.bashrc"
cp "$HOME/.bashrc" "$WORK/bashrc.orig"
check "install" install_sh
check "files installed and executable" test -x "$D/vdt" -a -x "$D/test_api" -a -x "$D/schemathesis_test" -a -x "$D/installer"
check "no .sh leftovers" test ! -e "$D/valhalla.sh" -a ! -e "$D/test_api.sh"
check "payloads and openapi.json" test -f "$D/tests/api_tests/payloads/auth/login.json" -a -f "$D/docs/openapi.json"
outputs "VERSION names the source" "^local $REPO " cat "$D/VERSION"
check "command link" test "$(readlink "$V")" = "$D/vdt"
check "one rc block in .bashrc" test "$(grep -c '>>> vdt >>>' "$HOME/.bashrc")" = 1
check "rc block keeps the user's lines" grep -q KEEP_ME "$HOME/.bashrc"
check "rc block adds ~/.local/bin to PATH" grep -qF 'export PATH="$HOME/.local/bin":"$PATH"' "$HOME/.bashrc"
if [[ $OS == Darwin ]]; then
  check "macOS: .bash_profile gets the block" grep -q '>>> vdt >>>' "$HOME/.bash_profile"
else
  check "Linux: no .bash_profile created" test ! -e "$HOME/.bash_profile"
fi
check "no .zshrc created for a bash user" test ! -e "$HOME/.zshrc"
outputs "config has a 64-hex TOKEN_SECRET" '^TOKEN_SECRET = "[0-9a-f]{64}"$' cat "$D/config"
outputs "config is private (600)" '^-rw-------' ls -l "$D/config"
outputs "rc block works in bash: vdt on PATH and completion loaded" "^$V function$" \
  bash -c 'source "$HOME/.bashrc"; printf "%s %s\n" "$(command -v vdt)" "$(type -t _vdt)"'

section "Installer: update, replace, failures"
secret_before="$(grep TOKEN_SECRET "$D/config")"
check "re-install" install_sh
check "still one rc block" test "$(grep -c '>>> vdt >>>' "$HOME/.bashrc")" = 1
check "re-install keeps TOKEN_SECRET" test "$(grep TOKEN_SECRET "$D/config")" = "$secret_before"
echo "KEEP=1" >>"$D/config"
mkdir -p "$D/dumps" && echo "dump" >"$D/dumps/keep.sql"
check "--replace" install_sh --replace
check "--replace keeps config edits" grep -q KEEP=1 "$D/config"
check "--replace keeps dumps/" test -f "$D/dumps/keep.sql"
fails "--replace with a bad source fails" "has no valhalla.sh" env VALHALLA_SOURCE=/nonexistent "$POSIX_SH" "$REPO/install.sh" --replace
check "...and leaves the install alone" test -x "$D/vdt" -a -L "$V"
fails "unknown option" "unknown option: --bogus" install_sh --bogus
fails "--ref without a value" "--ref needs a value" install_sh --ref
outputs "--help prints the header" "Valhalla Deployment Tool" install_sh --help
printf 'TOKEN_SECRET = "x"\n' >"$WORK/old.env"
mv "$D/config" "$WORK/config.bak"
cp "$WORK/old.env" "$D/.env"
check "old .env is renamed to config" sh -c "'$POSIX_SH' '$REPO/install.sh' >/dev/null && test -f '$D/config' -a ! -e '$D/.env'"
mv "$WORK/config.bak" "$D/config"

section "Installer: uninstall"
check "uninstall" "$POSIX_SH" "$D/installer" --uninstall
check "link removed" test ! -e "$V" -a ! -L "$V"
check ".bashrc restored byte for byte" diff "$WORK/bashrc.orig" "$HOME/.bashrc"
if [[ $OS == Darwin ]]; then
  check "macOS: .bash_profile cleaned" sh -c "! grep -q 'vdt' '$HOME/.bash_profile'"
fi
check "edited config kept" test -f "$D/config"
check "dumps/ kept" test -f "$D/dumps/keep.sql"
check "tool files removed" test ! -e "$D/vdt" -a ! -e "$D/tests/api_tests" -a ! -e "$D/docs"
outputs "uninstall again is harmless" "Removed the tool|Nothing installed" "$POSIX_SH" "$REPO/install.sh" --uninstall

section "Installer: zsh user, custom dirs, --no-rc"
fresh_home /bin/zsh
check "install --dir/--bin-dir (relative)" sh -c "cd '$HOME' && '$POSIX_SH' '$REPO/install.sh' --dir tool --bin-dir bin"
check "custom dir used" test -x "$HOME/tool/vdt"
check "custom link is absolute" test "$(readlink "$HOME/bin/vdt")" = "$HOME/tool/vdt"
check ".zshrc created for a zsh user" grep -q '>>> vdt >>>' "$HOME/.zshrc"
check "no .bashrc created for a zsh user" test ! -e "$HOME/.bashrc"
if command -v zsh >/dev/null 2>&1; then
  outputs "rc block works in zsh: vdt on PATH and completion loaded" "^$HOME/bin/vdt yes$" \
    zsh -c 'autoload -Uz compinit && compinit -u -d "$HOME/.zcompdump"; source "$HOME/.zshrc"; print -r -- "$(whence -p vdt) ${${(k)functions[_vdt]}:+yes}"'
fi
check "uninstall the default config (unedited) too" "$POSIX_SH" "$HOME/tool/installer" --uninstall --dir "$HOME/tool" --bin-dir "$HOME/bin"
check "...removes the whole dir" test ! -e "$HOME/tool"
check "...and empties .zshrc" test ! -s "$HOME/.zshrc"
fresh_home /bin/bash
check "install --no-rc" install_sh --no-rc
check "--no-rc wrote no rc file" test ! -e "$HOME/.bashrc" -a ! -e "$HOME/.zshrc" -a ! -e "$HOME/.bash_profile"
mkdir -p "$HOME/.local/bin" && rm -f "$V" && echo "mine" >"$V"
outputs "a real file at the link path is left alone" "isn't a link" install_sh --no-rc
check "...and kept" grep -q mine "$V"
rm -f "$V"
check "reinstall for the tool tests" install_sh --no-rc

# ── tool: basics ───────────────────────────────────────────────────────────
section "Tool: basics"
outputs "version (installed)" "^vdt local .* \(installed in $D\)$" vdt version
check "runs through the link with its own shebang" "$V" version
outputs "help" "Valhalla Deployment Tool" vdt help
outputs "help lists every command" "payload" vdt help
outputs "no command shows help" "Quick start" vdt
outputs "-h shows help" "Quick start" vdt -h
fails "unknown command" "unknown command: bogus" vdt bogus
fails "unknown option" "unknown option|--bogus" vdt --bogus status
fails "--raw outside payload" "--raw is a payload option" vdt status --raw
fails "--image-only outside update" "update options" vdt run --image-only
fails "bad --runtime" "runtime must be 'docker' or 'container'" vdt --runtime bogus status
fails "completion for an unknown shell" "supports zsh or bash" vdt completion fish
outputs "checkout copy: version says checkout" "\(checkout $REPO\)$" "$TOOL_BASH" "$REPO/scripts/vdt/valhalla.sh" version

section "Tool: completion"
outputs "bash: commands" "^payload$" "$TOOL_BASH" -c 'eval "$("$1" completion bash)"; COMP_WORDS=(vdt pay); COMP_CWORD=1; _vdt; printf "%s\n" "${COMPREPLY[@]}"' _ "$V"
outputs "bash: options" "^--persist$" "$TOOL_BASH" -c 'eval "$("$1" completion bash)"; COMP_WORDS=(vdt --pe); COMP_CWORD=1; _vdt; printf "%s\n" "${COMPREPLY[@]}"' _ "$V"
outputs "bash: --runtime values" "^container$" "$TOOL_BASH" -c 'eval "$("$1" completion bash)"; COMP_WORDS=(vdt --runtime c); COMP_CWORD=2; _vdt; printf "%s\n" "${COMPREPLY[@]}"' _ "$V"
if command -v zsh >/dev/null 2>&1; then
  check "zsh: script loads" zsh -c 'autoload -Uz compinit && compinit -u -d "$HOME/.zcompdump"; eval "$("$1" completion zsh)"; (( $+functions[_vdt] ))' _ "$V"
fi

section "Tool: payload"
outputs "list all groups (piped)" "providers \\(4 payloads\\)" vdt payload
outputs "list a group" "create" vdt payload clinics
outputs "show one" "Placeholders" vdt payload clinics create
outputs "group/name form" "Placeholders" vdt payload clinics/create.json
check "--raw is the file as-is" sh -c "\"\$0\" '$V' payload clinics create --raw | cmp -s - '$D/tests/api_tests/payloads/clinics/create.json'" "$TOOL_BASH"
all_filled=1
while IFS= read -r f; do
  rel="${f#"$D/tests/api_tests/payloads/"}"
  if ! out="$(vdt payload "${rel%.json}" --filled 2>/dev/null | jq -e . 2>&1)"; then
    all_filled=0; bad "--filled $rel is valid JSON" "$out"
  fi
done < <(find "$D/tests/api_tests/payloads" -name '*.json' | sort)
[[ $all_filled == 1 ]] && ok "--filled gives valid JSON for every payload"
fails "unknown group" "no payload group 'nope'" vdt payload nope
fails "unknown name" "no payload clinics/nope.json" vdt payload clinics nope
fails "--raw and --filled together" "can't be used together" vdt payload clinics create --raw --filled
fails "--raw without a name" "need a group and a name" vdt payload clinics --raw
fails "too many arguments" "usage: " vdt payload a b c

section "Tool: secret and env"
ENVF="$WORK/test.env"
printf '# mine\nSERVER_THREADS = 3\n' >"$ENVF"
outputs "secret adds TOKEN_SECRET" "Added TOKEN_SECRET" vdt --env-file "$ENVF" secret
outputs "...64 hex characters" '^TOKEN_SECRET = "[0-9a-f]{64}"$' cat "$ENVF"
s1="$(grep TOKEN_SECRET "$ENVF")"
fails "replacing needs --yes when piped" "re-run with --yes" vdt --env-file "$ENVF" secret
check "...and changes nothing" test "$(grep TOKEN_SECRET "$ENVF")" = "$s1"
outputs "secret --yes replaces it" "Replaced TOKEN_SECRET" vdt --env-file "$ENVF" secret --yes
check "...with a new value, keeping the other lines" sh -c "test \"\$(grep TOKEN_SECRET '$ENVF')\" != '$s1' && grep -q '^# mine' '$ENVF' && grep -q 'SERVER_THREADS = 3' '$ENVF'"
outputs "env reads the file" "^SERVER_THREADS=3$" vdt --env-file "$ENVF" env
outputs "env hides the secret" "^TOKEN_SECRET=<hidden>$" vdt --env-file "$ENVF" env
outputs "env uses the installed config by default" "Env file: $D/config" vdt env
outputs "a .env in the current directory wins" "Env file: $WORK/cwd/.env" sh -c "mkdir -p '$WORK/cwd' && cd '$WORK/cwd' && echo 'A=1' >.env && \"\$0\" '$V' env" "$TOOL_BASH"

# ── tool: container commands without a running container ──────────────────
section "Tool: no container (docker installed, daemon down)"
outputs "status: not created" "State: +not created" vdt status
for c in run update; do
  outputs "$c -n: dry run, pulls the image" "Dry run \(docker\), nothing changed|would:? *pull|pull docker.io/" vdt "$c" -n
done
outputs "run -n --persist: mounts dumps/" "/persist" vdt run -n --persist
outputs "run -n --port: uses the port" "18099" vdt run -n --port 18099
outputs "build -n: docker build from the checkout" "docker build -f $REPO/docker/Dockerfile" "$TOOL_BASH" "$REPO/scripts/vdt/valhalla.sh" build -n
fails "build from the installed copy needs a checkout" "docker/Dockerfile" vdt build -n
outputs "delete -n: nothing to delete" "doesn't exist" vdt delete -n
outputs "clean -n: lists nothing" "Dry run \(docker\): would remove container" vdt clean -n
outputs "test -n" "Dry run: would run the api tests" vdt test -n
outputs "install -n" "Dry run, nothing changed" vdt install -n
outputs "uninstall -n" "Dry run: would run $D/installer" vdt uninstall -n
for c in stop restart dump psql shell logs show stats outdated; do
  fails "$c: no container" "doesn't exist" vdt "$c"
done
fails "restore: needs a file" "usage: .* restore <file>" vdt restore
fails "restore: missing file" "file not found" vdt restore "$WORK/nope.sql"
fails "logs: bad count" "doesn't exist|takes a number" vdt logs abc

section "Tool: dependency checks"
# A PATH with every command except the runtimes (CI runners have docker in
# /usr/bin, so trimming PATH to the system dirs isn't enough).
NORT="$WORK/no-runtime"
mkdir -p "$NORT"
IFS=: read -r -a path_dirs <<<"$ORIG_PATH"
for d in "${path_dirs[@]}"; do
  [[ -d $d ]] || continue
  for f in "$d"/*; do
    n="${f##*/}"
    case "$n" in docker | container | podman) continue ;; esac
    [[ -x $f && ! -e $NORT/$n ]] && ln -s "$f" "$NORT/$n"
  done
done
fails "no runtime installed" "container or docker" env PATH="$NORT" VALHALLA_RUNTIME= "$TOOL_BASH" "$V" status
if command -v container >/dev/null 2>&1; then
  : # Apple container is installed (a developer Mac); nothing to check
elif [[ $OS == Darwin ]]; then
  fails "missing Apple container" "apple/container" env VALHALLA_RUNTIME=container "$TOOL_BASH" "$V" status
else
  fails "Apple container on Linux" "macOS only" env VALHALLA_RUNTIME=container "$TOOL_BASH" "$V" status
fi

section "Tool: uninstall through the tool"
fails "uninstall needs --yes when piped" "--yes" vdt uninstall
check "uninstall --yes" vdt uninstall --yes
check "...removed the tool" test ! -e "$D/vdt" -a ! -L "$V"

# ── helper scripts ─────────────────────────────────────────────────────────
section "Helper scripts"
outputs "get_tag.sh: VALHALLA_VERSION" '^-DGIT_TAG="9.9.9"$' env VALHALLA_VERSION=9.9.9 "$TOOL_BASH" "$REPO/get_tag.sh"
outputs "get_tag.sh: from git" '^-DGIT_TAG=".*"$' sh -c "cd '$REPO' && \"\$0\" ./get_tag.sh" "$TOOL_BASH"

# run_release.sh: a fake build/Release/server that prints its env, in a copy
# of the script so the checkout isn't touched.
RR="$WORK/rr"
mkdir -p "$RR/build/Release"
cp "$REPO/run_release.sh" "$RR/"
fails "run_release.sh: no binary" "not found. Run scripts/build/build.sh" "$TOOL_BASH" "$REPO/scripts/run/run_release.sh"
printf '#!/bin/sh\necho "args=$*"; env | grep -E "^(A|B|C|D|E|F)=" | sort\n' >"$RR/build/Release/server"
chmod +x "$RR/build/Release/server"
printf '# comment\nA=1\n  B = two words  \nexport C="quoted # not a comment"\nD='"'"'single'"'"'\r\n\nE=$(echo injected)\nbad line\n' >"$RR/.env"
out="$("$TOOL_BASH" "$REPO/scripts/run/run_release.sh" --flag 2>&1)"
expect="$(printf 'args=--flag\nA=1\nB=two words\nC=quoted # not a comment\nD=single\nE=$(echo injected)')"
if [[ $out == "$expect" ]]; then ok "run_release.sh: parses .env without running it"; else bad "run_release.sh: parses .env without running it" "$out"; fi

# test_api.sh against a port nothing listens on: fails fast, doesn't hang.
fails "test_api.sh: no server -> fails" "hello|FAIL|fail|error" env BASE_URL=http://127.0.0.1:9/api/v2 "$TOOL_BASH" "$REPO/scripts/test/test_api.sh"

# ── summary ────────────────────────────────────────────────────────────────
printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
if [[ $FAIL -gt 0 ]]; then
  printf 'Failed:\n'
  printf '  %s\n' "${FAILED[@]}"
  exit 1
fi
