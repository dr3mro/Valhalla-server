#!/usr/bin/env bash
# /**
#  * @file test_tool.sh
#  * @brief Offline regression suite for install.sh, the vdt tool and helper scripts: syntax checks, install/update/uninstall in throwaway HOMEs, completion/payload/env/container dry-run tests, using a stub docker and no network.
#  * @details Run offline regression tests for the installer, the vdt tool and the helper scripts using a fake docker and throwaway home directories.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * CI runs this before merging changes to any shell script in the repository.
#  * It needs no network and no container daemon, so it works on any developer machine.
#  * Without it, install/uninstall bugs and broken completions would only be found by users.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash functions: https://www.gnu.org/software/bash/manual/bash.html
#  * - jq: https://jqlang.github.io/jq/manual/
#  * - git ls-files: https://git-scm.com/docs/git-ls-files
#  * - POSIX sh: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
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

# -u and pipefail are on, but -e is deliberately absent so negative tests
# can assert that commands fail.
# shellcheck disable=SC2016  # single-quoted code runs in a child shell
set -uo pipefail  # u and pipefail catch mistakes; no -e so negative tests can assert that commands fail.

# Resolve the repository root once for all test paths.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # BASH_SOURCE[0] is this script; ../.. climbs to the repository root.
# TOOL_BASH lets CI run the tool with an older bash (e.g. macOS /bin/bash 3.2).
TOOL_BASH="${TOOL_BASH:-bash}"  # TOOL_BASH lets CI test with an older bash, such as macOS's 3.2.
if [[ -z ${POSIX_SH:-} ]]; then  # -z checks whether the variable is empty before choosing a default.
  if command -v dash >/dev/null 2>&1; then POSIX_SH="dash"; else POSIX_SH="sh"; fi  # prefer dash because it is a strict POSIX shell.
fi
OS="$(uname -s)"  # the OS name selects platform-specific expectations (Linux vs macOS).

# Counters and the failed-test list drive the final summary and exit status.
PASS=0  # number of passing assertions.
FAIL=0  # number of failing assertions.
FAILED=()  # names of failed assertions for the final report.

# ── helpers ────────────────────────────────────────────────────────────────
# @brief Print a section heading.
# @param Heading text.
# @return Nothing.
section() { printf '\n== %s ==\n' "$1"; }  # print a labeled section heading.

# @brief Record and print a passing assertion.
# @param Test name.
# @return Nothing.
ok() { PASS=$((PASS + 1)); printf '  ok    %s\n' "$1"; }  # add one to PASS and print the test name.
# @brief Record a failing assertion and print up to 20 lines of its output.
# @param Test name.
# @param Optional output to display.
# @return Nothing.
bad() {
  FAIL=$((FAIL + 1)); FAILED+=("$1"); printf '  FAIL  %s\n' "$1"  # add one to FAIL, remember the name and print it.
  if [[ -n ${2:-} ]]; then printf '%s\n' "$2" | head -n 20 | sed 's/^/        | /'; fi  # show up to 20 lines of output, each prefixed with '|', to aid debugging.
}

# check NAME CMD...: CMD succeeds.
# @brief Assert that a command succeeds.
# @param Test name, then command and arguments.
# @return Nothing.
check() {
  local name="$1" out; shift  # local keeps these variables inside the helper.
  if out="$("$@" 2>&1)"; then ok "$name"; else bad "$name" "$out"; fi  # capture the command's output; success is ok, failure is bad.
}

# fails NAME PATTERN CMD...: CMD fails and its output matches PATTERN (ERE).
# @brief Assert a command fails and its output matches an ERE pattern.
# @param name, ERE pattern, then command and arguments.
# @return Nothing.
fails() {
  local name="$1" pat="$2" out; shift 2  # name and regex come first, then the command to run.
  if out="$("$@" 2>&1)"; then bad "$name (expected failure)" "$out"  # for this helper, a command that succeeds is itself a failure.
  elif printf '%s' "$out" | grep -qE -- "$pat"; then ok "$name"  # grep -qE checks the output against an extended regular expression.
  else bad "$name (output doesn't match /$pat/)" "$out"
  fi
}

# outputs NAME PATTERN CMD...: CMD succeeds and its output matches PATTERN.
# @brief Assert a command succeeds and its output matches an ERE pattern.
# @param name, ERE pattern, then command and arguments.
# @return Nothing.
outputs() {
  local name="$1" pat="$2" out; shift 2  # same regex check, but here the command must also succeed.
  if ! out="$("$@" 2>&1)"; then bad "$name (exit != 0)" "$out"  # if the command fails, record the failure.
  elif printf '%s' "$out" | grep -qE -- "$pat"; then ok "$name"
  else bad "$name (output doesn't match /$pat/)" "$out"
  fi
}

# Every test runs in a throwaway HOME, with a stub docker first on PATH.
# One throwaway directory keeps all tests off the real home and checkout.
WORK="$(mktemp -d)"  # mktemp -d creates a throwaway directory for all test artifacts.
# Clean the throwaway directory when the suite exits.
trap 'rm -rf "$WORK"' EXIT  # remove it on exit so no test files are left behind.
STUB="$WORK/stub"  # path where the fake docker program will live.
mkdir -p "$STUB"  # create the stub directory.
# A docker that always fails simulates 'installed but daemon down', exercising
# container paths offline through their dry runs and no-container errors.
cat >"$STUB/docker" <<'EOF'
#!/bin/sh
echo "Cannot connect to the Docker daemon at unix:///var/run/docker.sock. Is the docker daemon running?" >&2
exit 1
EOF
chmod +x "$STUB/docker"  # make the stub executable so PATH can find it.
# Keep the original PATH to later build a runtime-free PATH for dependency checks.
ORIG_PATH="$PATH"  # remember the real PATH before putting the stub first.
BASE_PATH="$STUB:$PATH"  # put the stub first so every 'docker' call hits the fake program.

# fresh_home SHELL: new empty HOME with SHELL as the login shell.
# @brief Create an isolated HOME with a given login shell and reset VDT path globals.
# @param Login shell path.
# @return Nothing; sets HOME, SHELL, D and V.
fresh_home() {
  export HOME="$WORK/home-$RANDOM$RANDOM" SHELL="$1"  # give this test a fresh HOME and login shell; no real user files are touched.
  mkdir -p "$HOME"  # create that HOME directory.
  D="$HOME/.local/share/vdt"  # D is the folder where vdt files would be installed.
  V="$HOME/.local/bin/vdt"  # V is the path of the vdt command link.
}

# Put the stub docker first on PATH for every test.
export PATH="$BASE_PATH"  # activate the stub-first PATH for all following tests.
# Force local-source installs and disable prompts, update checks and completion side effects.
export VALHALLA_SOURCE="$REPO" VALHALLA_NO_COMPLETION=1 VALHALLA_NO_UPDATE_CHECK=1  # force local-source installs and disable completion/update side effects.
export VALHALLA_RUNTIME=docker TERM=dumb  # choose the docker runtime and a non-interactive terminal.
# Prevent host-specific VDT settings from leaking into the test environment.
unset VALHALLA_ENV_FILE VALHALLA_INSTALL_DIR VALHALLA_BIN_DIR VALHALLA_REF GITHUB_TOKEN NO_COLOR  # remove host-specific VDT settings that could change the results.
# Run from a directory without a .env so it cannot override the test config.
cd "$WORK" || exit 1 # no .env in the current directory  # run from the empty work directory so no local .env can interfere.

# @brief Run the installed vdt script with the chosen bash.
# @param vdt arguments.
# @return vdt's status.
vdt() { "$TOOL_BASH" "$V" "$@"; }  # run the installed vdt script with the chosen bash.
# @brief Run install.sh with the chosen POSIX shell.
# @param Installer arguments.
# @return Installer's status.
install_sh() { "$POSIX_SH" "$REPO/install.sh" "$@"; }  # run install.sh with the chosen POSIX shell.

echo "OS: $OS  tool bash: $("$TOOL_BASH" -c 'echo $BASH_VERSION')  installer sh: $POSIX_SH"  # print which shells are being used, for easier debugging.

# ── syntax ─────────────────────────────────────────────────────────────────
section "Syntax"  # begin the syntax-check section.
# Check syntax of every tracked .sh using the shell named in its shebang.
while IFS= read -r f; do  # loop over every tracked .sh file.
  case "$(head -n 1 "$REPO/$f")" in  # read the first line (shebang) to choose the right syntax checker.
    *bash*) check "bash -n $f" "$TOOL_BASH" -n "$REPO/$f" ;;  # bash -n only parses the file; it never executes it.
    *) check "$POSIX_SH -n $f" "$POSIX_SH" -n "$REPO/$f" ;;
  esac
done < <(git -C "$REPO" ls-files '*.sh')  # git ls-files supplies the list; <(...) keeps the counters in this shell.

# ── installer ──────────────────────────────────────────────────────────────
section "Installer: install (bash login shell)"  # start a fresh HOME with bash as the login shell.
fresh_home /bin/bash
printf 'export KEEP_ME=1\n' >"$HOME/.bashrc"  # put a user line in .bashrc to check the installer preserves it.
cp "$HOME/.bashrc" "$WORK/bashrc.orig"  # save a copy so uninstall can be compared byte for byte.
check "install" install_sh  # run a local-source install.
check "files installed and executable" test -x "$D/vdt" -a -x "$D/test_api" -a -x "$D/schemathesis_test" -a -x "$D/installer"  # -x checks that all four managed programs are executable.
check "no .sh leftovers" test ! -e "$D/valhalla.sh" -a ! -e "$D/test_api.sh"  # old .sh-named leftovers must be gone.
check "payloads and openapi.json" test -f "$D/tests/api_tests/payloads/auth/login.json" -a -f "$D/docs/openapi.json"  # payload JSON and the OpenAPI file must be installed.
outputs "VERSION names the source" "^local $REPO " cat "$D/VERSION"  # VERSION should record the local source path.
check "command link" test "$(readlink "$V")" = "$D/vdt"  # the command should be a link pointing at the installed vdt.
check "one rc block in .bashrc" test "$(grep -c '>>> vdt >>>' "$HOME/.bashrc")" = 1  # there must be exactly one managed block in .bashrc.
check "rc block keeps the user's lines" grep -q KEEP_ME "$HOME/.bashrc"  # the user's KEEP_ME line must survive the install.
check "rc block adds ~/.local/bin to PATH" grep -qF 'export PATH="$HOME/.local/bin":"$PATH"' "$HOME/.bashrc"  # the installer's PATH line must be present.
if [[ $OS == Darwin ]]; then  # on macOS the installer also manages .bash_profile.
  check "macOS: .bash_profile gets the block" grep -q '>>> vdt >>>' "$HOME/.bash_profile"  # that file should get the managed block too.
else
  check "Linux: no .bash_profile created" test ! -e "$HOME/.bash_profile"  # Linux should not create a .bash_profile.
fi
check "no .zshrc created for a bash user" test ! -e "$HOME/.zshrc"  # a bash user should not get a .zshrc.
outputs "config has a 64-hex TOKEN_SECRET" '^TOKEN_SECRET = "[0-9a-f]{64}"$' cat "$D/config"  # the config should contain a 64-character hex token secret.
outputs "config is private (600)" '^-rw-------' ls -l "$D/config"  # the config must be private, mode 600.
# Source .bashrc in a child shell to verify PATH and completion are loaded.
outputs "rc block works in bash: vdt on PATH and completion loaded" "^$V function$" \
  bash -c 'source "$HOME/.bashrc"; printf "%s %s\n" "$(command -v vdt)" "$(type -t _vdt)"'
  # the bash-specific check runs only on macOS.
section "Installer: update, replace, failures"
secret_before="$(grep TOKEN_SECRET "$D/config")"  # remember the secret so an update can be checked for stability.
check "re-install" install_sh  # reinstalling should be safe.
check "still one rc block" test "$(grep -c '>>> vdt >>>' "$HOME/.bashrc")" = 1
check "re-install keeps TOKEN_SECRET" test "$(grep TOKEN_SECRET "$D/config")" = "$secret_before"  # the secret should not change on a plain update.
echo "KEEP=1" >>"$D/config"  # edit the config to simulate a user change.
mkdir -p "$D/dumps" && echo "dump" >"$D/dumps/keep.sql"  # add a dump file that must survive --replace.
check "--replace" install_sh --replace  # --replace downloads first, then swaps the old install for the new one.
check "--replace keeps config edits" grep -q KEEP=1 "$D/config"  # user edits survive the replacement.
check "--replace keeps dumps/" test -f "$D/dumps/keep.sql"  # the dumps folder survives too.
fails "--replace with a bad source fails" "has no valhalla.sh" env VALHALLA_SOURCE=/nonexistent "$POSIX_SH" "$REPO/install.sh" --replace  # a bad local source must fail with a clear message.
check "...and leaves the install alone" test -x "$D/vdt" -a -L "$V"  # and must leave the existing install untouched.
fails "unknown option" "unknown option: --bogus" install_sh --bogus  # unknown options must be rejected.
fails "--ref without a value" "--ref needs a value" install_sh --ref  # --ref without a value must be rejected.
outputs "--help prints the header" "Valhalla Deployment Tool" install_sh --help  # --help must print the header text.
printf 'TOKEN_SECRET = "x"\n' >"$WORK/old.env"  # prepare an old-style .env file for the migration test.
mv "$D/config" "$WORK/config.bak"  # move config aside so the .env can be picked up.
cp "$WORK/old.env" "$D/.env"  # place the legacy .env where the installer expects config.
check "old .env is renamed to config" sh -c "'$POSIX_SH' '$REPO/install.sh' >/dev/null && test -f '$D/config' -a ! -e '$D/.env'"  # the installer should rename .env to config.
mv "$WORK/config.bak" "$D/config"  # restore the original config.

section "Installer: uninstall"
check "uninstall" "$POSIX_SH" "$D/installer" --uninstall  # uninstall using the copy installed under the name 'installer'.
check "link removed" test ! -e "$V" -a ! -L "$V"  # the command link must be gone.
check ".bashrc restored byte for byte" diff "$WORK/bashrc.orig" "$HOME/.bashrc"  # .bashrc must be byte-for-byte the saved original.
if [[ $OS == Darwin ]]; then
  check "macOS: .bash_profile cleaned" sh -c "! grep -q 'vdt' '$HOME/.bash_profile'"  # macOS .bash_profile must have no vdt lines left.
fi
check "edited config kept" test -f "$D/config"  # an edited config is kept on uninstall.
check "dumps/ kept" test -f "$D/dumps/keep.sql"  # the dumps folder is kept.
check "tool files removed" test ! -e "$D/vdt" -a ! -e "$D/tests/api_tests" -a ! -e "$D/docs"  # managed program files are removed.
outputs "uninstall again is harmless" "Removed the tool|Nothing installed" "$POSIX_SH" "$REPO/install.sh" --uninstall  # uninstalling twice must be harmless.

section "Installer: zsh user, custom dirs, --no-rc"
fresh_home /bin/zsh  # start a fresh HOME with zsh as the login shell.
check "install --dir/--bin-dir (relative)" sh -c "cd '$HOME' && '$POSIX_SH' '$REPO/install.sh' --dir tool --bin-dir bin"  # install into custom relative directories.
check "custom dir used" test -x "$HOME/tool/vdt"  # the custom directory was actually used.
check "custom link is absolute" test "$(readlink "$HOME/bin/vdt")" = "$HOME/tool/vdt"  # the link is absolute even though the input paths were relative.
check ".zshrc created for a zsh user" grep -q '>>> vdt >>>' "$HOME/.zshrc"  # a zsh user gets a .zshrc.
check "no .bashrc created for a zsh user" test ! -e "$HOME/.bashrc"  # a zsh user does not get a .bashrc.
if command -v zsh >/dev/null 2>&1; then  # only run zsh-specific checks if zsh is installed.
  # Verify PATH and the completion function inside zsh.
  outputs "rc block works in zsh: vdt on PATH and completion loaded" "^$HOME/bin/vdt yes$" \
    zsh -c 'autoload -Uz compinit && compinit -u -d "$HOME/.zcompdump"; source "$HOME/.zshrc"; print -r -- "$(whence -p vdt) ${${(k)functions[_vdt]}:+yes}"'
fi
check "uninstall the default config (unedited) too" "$POSIX_SH" "$HOME/tool/installer" --uninstall --dir "$HOME/tool" --bin-dir "$HOME/bin"  # uninstall using the custom directories.
check "...removes the whole dir" test ! -e "$HOME/tool"  # the whole custom directory is removed.
check "...and empties .zshrc" test ! -s "$HOME/.zshrc"  # the .zshrc is emptied again.
fresh_home /bin/bash  # go back to a bash HOME.
check "install --no-rc" install_sh --no-rc  # install with --no-rc.
check "--no-rc wrote no rc file" test ! -e "$HOME/.bashrc" -a ! -e "$HOME/.zshrc" -a ! -e "$HOME/.bash_profile"  # no rc files should be created at all.
mkdir -p "$HOME/.local/bin" && rm -f "$V" && echo "mine" >"$V"  # put a real file where the command link would go.
outputs "a real file at the link path is left alone" "isn't a link" install_sh --no-rc  # the installer should refuse to replace a real file.
check "...and kept" grep -q mine "$V"  # and leave that file intact.
rm -f "$V"  # remove the file.
check "reinstall for the tool tests" install_sh --no-rc  # reinstall so the later tool tests have vdt available.

# ── tool: basics ───────────────────────────────────────────────────────────
section "Tool: basics"
outputs "version (installed)" "^vdt local .* \(installed in $D\)$" vdt version  # version should say 'local ... (installed in ...)'.
check "runs through the link with its own shebang" "$V" version  # the link's own shebang should work.
outputs "help" "Valhalla Deployment Tool" vdt help  # help should print the tool banner.
outputs "help lists every command" "payload" vdt help  # help must list every command, including payload.
outputs "no command shows help" "Quick start" vdt  # running with no command shows the quick-start help.
outputs "-h shows help" "Quick start" vdt -h  # -h also shows the quick-start help.
fails "unknown command" "unknown command: bogus" vdt bogus  # an unknown command must fail.
fails "unknown option" "unknown option|--bogus" vdt --bogus status  # an unknown option must fail.
fails "--raw outside payload" "--raw is a payload option" vdt status --raw  # --raw is only valid inside the payload command.
fails "--image-only outside update" "update options" vdt run --image-only  # --image-only is only valid for update.
fails "bad --runtime" "runtime must be 'docker' or 'container'" vdt --runtime bogus status  # a bad runtime value must be rejected.
fails "completion for an unknown shell" "supports zsh or bash" vdt completion fish  # completion for an unknown shell must be rejected.
outputs "checkout copy: version says checkout" "\(checkout $REPO\)$" "$TOOL_BASH" "$REPO/scripts/vdt/valhalla.sh" version  # a checkout copy should report 'checkout' rather than 'installed'.

section "Tool: completion"
outputs "bash: commands" "^payload$" "$TOOL_BASH" -c 'eval "$("$1" completion bash)"; COMP_WORDS=(vdt pay); COMP_CWORD=1; _vdt; printf "%s\n" "${COMPREPLY[@]}"' _ "$V"  # evaluate bash completion and check that 'pay' suggests payload.
outputs "bash: options" "^--persist$" "$TOOL_BASH" -c 'eval "$("$1" completion bash)"; COMP_WORDS=(vdt --pe); COMP_CWORD=1; _vdt; printf "%s\n" "${COMPREPLY[@]}"' _ "$V"  # check completion of the --pe option prefix.
outputs "bash: --runtime values" "^container$" "$TOOL_BASH" -c 'eval "$("$1" completion bash)"; COMP_WORDS=(vdt --runtime c); COMP_CWORD=2; _vdt; printf "%s\n" "${COMPREPLY[@]}"' _ "$V"  # check completion of values for --runtime.
if command -v zsh >/dev/null 2>&1; then
  check "zsh: script loads" zsh -c 'autoload -Uz compinit && compinit -u -d "$HOME/.zcompdump"; eval "$("$1" completion zsh)"; (( $+functions[_vdt] ))' _ "$V"  # the zsh completion script should define _vdt.
fi

section "Tool: payload"
outputs "list all groups (piped)" "providers \\(4 payloads\\)" vdt payload  # list all payload groups.
outputs "list a group" "create" vdt payload clinics  # list one payload group.
outputs "show one" "Placeholders" vdt payload clinics create  # show one payload.
outputs "group/name form" "Placeholders" vdt payload clinics/create.json  # the group/name form should also work.
check "--raw is the file as-is" sh -c "\"\$0\" '$V' payload clinics create --raw | cmp -s - '$D/tests/api_tests/payloads/clinics/create.json'" "$TOOL_BASH"  # --raw must print the payload file unchanged.
all_filled=1  # start optimistic for the filled-payload loop.
while IFS= read -r f; do  # loop over every payload JSON file.
  rel="${f#"$D/tests/api_tests/payloads/"}"  # strip the install prefix to get group/name.
  if ! out="$(vdt payload "${rel%.json}" --filled 2>/dev/null | jq -e . 2>&1)"; then  # jq -e verifies the filled payload is valid JSON.
    all_filled=0; bad "--filled $rel is valid JSON" "$out"  # record a failure if it is not valid.
  fi
done < <(find "$D/tests/api_tests/payloads" -name '*.json' | sort)  # sort the files for a deterministic order.
[[ $all_filled == 1 ]] && ok "--filled gives valid JSON for every payload"  # one pass line if every payload passed.
fails "unknown group" "no payload group 'nope'" vdt payload nope  # an unknown payload group must fail.
fails "unknown name" "no payload clinics/nope.json" vdt payload clinics nope  # an unknown payload name must fail.
fails "--raw and --filled together" "can't be used together" vdt payload clinics create --raw --filled  # --raw and --filled cannot be used together.
fails "--raw without a name" "need a group and a name" vdt payload clinics --raw  # --raw needs both a group and a name.
fails "too many arguments" "usage: " vdt payload a b c  # too many arguments must be rejected.

section "Tool: secret and env"
ENVF="$WORK/test.env"  # path to the temporary env file used below.
printf '# mine\nSERVER_THREADS = 3\n' >"$ENVF"  # write a small config file with a comment and a setting.
outputs "secret adds TOKEN_SECRET" "Added TOKEN_SECRET" vdt --env-file "$ENVF" secret  # the secret command should add TOKEN_SECRET.
outputs "...64 hex characters" '^TOKEN_SECRET = "[0-9a-f]{64}"$' cat "$ENVF"  # check it is 64 hex characters.
s1="$(grep TOKEN_SECRET "$ENVF")"  # remember it for the next checks.
fails "replacing needs --yes when piped" "re-run with --yes" vdt --env-file "$ENVF" secret  # replacing a secret needs --yes when piped.
check "...and changes nothing" test "$(grep TOKEN_SECRET "$ENVF")" = "$s1"  # the file must be unchanged when --yes is missing.
outputs "secret --yes replaces it" "Replaced TOKEN_SECRET" vdt --env-file "$ENVF" secret --yes  # --yes should replace the secret.
check "...with a new value, keeping the other lines" sh -c "test \"\$(grep TOKEN_SECRET '$ENVF')\" != '$s1' && grep -q '^# mine' '$ENVF' && grep -q 'SERVER_THREADS = 3' '$ENVF'"  # the new value must differ while other lines survive.
outputs "env reads the file" "^SERVER_THREADS=3$" vdt --env-file "$ENVF" env  # the env command should read the file's settings.
outputs "env hides the secret" "^TOKEN_SECRET=<hidden>$" vdt --env-file "$ENVF" env  # the secret itself must be hidden.
outputs "env uses the installed config by default" "Env file: $D/config" vdt env  # without --env-file, the installed config is used.
outputs "a .env in the current directory wins" "Env file: $WORK/cwd/.env" sh -c "mkdir -p '$WORK/cwd' && cd '$WORK/cwd' && echo 'A=1' >.env && \"\$0\" '$V' env" "$TOOL_BASH"  # a .env in the current directory takes priority.

# ── tool: container commands without a running container ──────────────────
section "Tool: no container (docker installed, daemon down)"
outputs "status: not created" "State: +not created" vdt status  # status should report that no container has been created.
for c in run update; do  # run the same checks for both run and update.
  outputs "$c -n: dry run, pulls the image" "Dry run \(docker\), nothing changed|would:? *pull|pull docker.io/" vdt "$c" -n  # a dry run should say nothing changed or that it would pull the image.
done
outputs "run -n --persist: mounts dumps/" "/persist" vdt run -n --persist  # --persist should mention the persistence mount.
outputs "run -n --port: uses the port" "18099" vdt run -n --port 18099  # --port should be reflected in the dry run.
outputs "build -n: docker build from the checkout" "docker build -f $REPO/docker/Dockerfile" "$TOOL_BASH" "$REPO/scripts/vdt/valhalla.sh" build -n  # build -n from a checkout should use docker build.
fails "build from the installed copy needs a checkout" "docker/Dockerfile" vdt build -n  # the installed copy cannot build without a checkout.
outputs "delete -n: nothing to delete" "doesn't exist" vdt delete -n  # delete -n on a missing container.
outputs "clean -n: lists nothing" "Dry run \(docker\): would remove container" vdt clean -n  # clean -n should list nothing to remove.
outputs "test -n" "Dry run: would run the api tests" vdt test -n  # test -n dry run.
outputs "install -n" "Dry run, nothing changed" vdt install -n  # install -n dry run.
outputs "uninstall -n" "Dry run: would run $D/installer" vdt uninstall -n  # uninstall -n dry run.
for c in stop restart dump psql shell logs show stats outdated; do  # list commands that require a real container.
  fails "$c: no container" "doesn't exist" vdt "$c"  # each should fail with a 'does not exist' message.
done
fails "restore: needs a file" "usage: .* restore <file>" vdt restore  # restore requires a dump file argument.
fails "restore: missing file" "file not found" vdt restore "$WORK/nope.sql"  # restore must fail when the file is missing.
fails "logs: bad count" "doesn't exist|takes a number" vdt logs abc  # logs requires a numeric count.

section "Tool: dependency checks"
# A PATH with every command except the runtimes (CI runners have docker in
# /usr/bin, so trimming PATH to the system dirs isn't enough).
NORT="$WORK/no-runtime"  # directory for a PATH that has no container runtimes.
mkdir -p "$NORT"  # create that directory.
IFS=: read -r -a path_dirs <<<"$ORIG_PATH"  # split the original PATH into its directories.
for d in "${path_dirs[@]}"; do  # loop over each directory.
  [[ -d $d ]] || continue  # skip directories that do not exist.
  for f in "$d"/*; do  # loop over the programs in the directory.
    n="${f##*/}"  # extract just the program name.
    case "$n" in docker | container | podman) continue ;; esac  # skip the three known container runtimes.
    [[ -x $f && ! -e $NORT/$n ]] && ln -s "$f" "$NORT/$n"  # link every other program into the runtime-free PATH.
  done
done
fails "no runtime installed" "container or docker" env PATH="$NORT" VALHALLA_RUNTIME= "$TOOL_BASH" "$V" status  # without a runtime installed, status must fail.
if command -v container >/dev/null 2>&1; then  # check whether Apple's 'container' command is installed.
  : # Apple container is installed (a developer Mac); nothing to check  # on a developer Mac there is nothing extra to check.
elif [[ $OS == Darwin ]]; then
  fails "missing Apple container" "apple/container" env VALHALLA_RUNTIME=container "$TOOL_BASH" "$V" status  # on macOS without it, expect an Apple-container error.
else
  fails "Apple container on Linux" "macOS only" env VALHALLA_RUNTIME=container "$TOOL_BASH" "$V" status  # on Linux, expect a macOS-only error.
fi

section "Tool: uninstall through the tool"
fails "uninstall needs --yes when piped" "--yes" vdt uninstall  # uninstall needs --yes when output is piped.
check "uninstall --yes" vdt uninstall --yes  # uninstall with --yes should succeed.
check "...removed the tool" test ! -e "$D/vdt" -a ! -L "$V"  # the tool must be gone afterwards.

# ── helper scripts ─────────────────────────────────────────────────────────
section "Helper scripts"
outputs "get_tag.sh: VALHALLA_VERSION" '^-DGIT_TAG="9.9.9"$' env VALHALLA_VERSION=9.9.9 "$TOOL_BASH" "$REPO/scripts/build/get_tag.sh"  # get_tag.sh must honour VALHALLA_VERSION.
outputs "get_tag.sh: from git" '^-DGIT_TAG=".*"$' sh -c "cd '$REPO' && \"\$0\" ./scripts/build/get_tag.sh" "$TOOL_BASH"  # otherwise get_tag.sh must derive the tag from git.

# run_release.sh: a fake build/Release/server that prints its env, in a copy
# of the script so the checkout isn't touched.
RR="$WORK/rr"  # scratch directory for a test copy of run_release.sh.
mkdir -p "$RR/build/Release" "$RR/scripts/run"  # create fake build and script folders.
cp "$REPO/scripts/run/run_release.sh" "$RR/scripts/run/"  # copy the script so the checkout is not modified.
fails "run_release.sh: no binary" "not found. Run scripts/build/build.sh" "$TOOL_BASH" "$RR/scripts/run/run_release.sh"  # without a server binary the script must fail.
printf '#!/bin/sh\necho "args=$*"; env | grep -E "^(A|B|C|D|E|F)=" | sort\n' >"$RR/build/Release/server"  # create a fake server that prints its arguments and selected env vars.
chmod +x "$RR/build/Release/server"  # make the fake server executable.
printf '# comment\nA=1\n  B = two words  \nexport C="quoted # not a comment"\nD='"'"'single'"'"'\r\n\nE=$(echo injected)\nbad line\n' >"$RR/.env"  # write a tricky .env to test safe parsing.
out="$("$TOOL_BASH" "$RR/scripts/run/run_release.sh" --flag 2>&1)"  # run the script with an extra flag.
expect="$(printf 'args=--flag\nA=1\nB=two words\nC=quoted # not a comment\nD=single\nE=$(echo injected)')"  # build the expected output string.
if [[ $out == "$expect" ]]; then ok "run_release.sh: parses .env without running it"; else bad "run_release.sh: parses .env without running it" "$out"; fi  # compare actual output with the expectation.

# test_api.sh against a port nothing listens on: fails fast, doesn't hang.
fails "test_api.sh: no server -> fails" "hello|FAIL|fail|error" env BASE_URL=http://127.0.0.1:9/api/v2 "$TOOL_BASH" "$REPO/scripts/test/test_api.sh"  # test_api against a dead port must fail fast, not hang.

# ── summary ────────────────────────────────────────────────────────────────
printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"  # print the pass/fail totals.
# Exit non-zero when any assertion failed so CI picks it up.
if [[ $FAIL -gt 0 ]]; then  # exit 1 if any assertion failed so CI notices.
  printf 'Failed:\n'
  printf '  %s\n' "${FAILED[@]}"  # list the names of the failing assertions.
  exit 1
fi
