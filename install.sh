#!/bin/sh
# /**
#  * @file install.sh
#  * @brief Self-installer/updater for the Valhalla Deployment Tool (vdt): downloads vdt and its helper files from GitHub (or a local checkout), installs them under ~/.local with a symlinked command and shell completion, manages a default config with a generated JWT secret, and supports --uninstall/--replace.
#  * @details Install or remove the Valhalla Deployment Tool (vdt) under the user's home directory, from GitHub or a local checkout.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * Users run this once to get the 'vdt' command, then re-run it to update.
#  * It also supports --uninstall and --replace and manages shell completion.
#  * Without it, deploying the server would require manually downloading and linking many files.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - GitHub REST API: https://docs.github.com/en/rest
#  * - POSIX sh: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
#  * - Bash startup files: https://www.gnu.org/software/bash/manual/html_node/Bash-Startup-Files.html
#  * - curl: https://curl.se/docs/manual.html
#  * - awk: https://www.gnu.org/software/gawk/manual/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Install the Valhalla Deployment Tool (vdt) straight from the GitHub repo:
# the `vdt` command (the repo's valhalla.sh) as a link in ~/.local/bin, and
# its files (scripts without the .sh extension, test payloads, a copy of
# this installer) in ~/.local/share/vdt. Adds ~/.local/bin to PATH if needed
# and tab completion in ~/.zshrc and/or ~/.bashrc.
#
#   curl -fsSL https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh | sh
#
# Re-run the same command, or `vdt install`, to update to the latest version.
#
# Options (after `sh -s --` when piping, e.g. `| sh -s -- --uninstall`):
#   --ref REF       branch, tag or commit to install (default: main)
#   --dir DIR       where the files go     (default: ~/.local/share/vdt)
#   --bin-dir DIR   where the command goes (default: ~/.local/bin)
#   --no-rc         don't touch ~/.zshrc / ~/.bashrc
#   --uninstall     remove the tool and the rc lines (keeps config and dumps/);
#                   same as `vdt uninstall`
#   --replace       download first, then remove the current copy (like
#                   --uninstall) and install; nothing is removed if the
#                   download fails. Used by `vdt install`.
#   -h, --help      show this help
#
# Environment: VALHALLA_REF, VALHALLA_INSTALL_DIR, VALHALLA_BIN_DIR,
# GITHUB_TOKEN (optional,
# avoids GitHub's anonymous API rate limit), VALHALLA_SOURCE (install from a
# local checkout instead of downloading; for testing).
#
# Installs: vdt (valhalla.sh), test_api, schemathesis_test, installer
# (a copy of this script, used by `vdt uninstall`), tests/api_tests/payloads/,
# docs/openapi.json, and a default `config` (settings passed to the server)
# if there isn't one. Your config and dumps/ in the install directory are never
# touched.

# -e stops on the first failing command and -u on unset variables, so a
# broken download or half-written install is not left behind.
set -eu  # e = stop on the first error, u = error on unset variables.

# Red "error:" when stderr is a terminal.
if [ -t 2 ]; then C_ERR=$(printf '\033[31m'); RST=$(printf '\033[0m'); else C_ERR=; RST=; fi  # -t 2 checks whether stderr is a terminal; colors are only useful there.


REPO="dr3mro/Valhalla-server"  # GitHub's 'owner/repo' pair, used to build the download URLs.
# Branch/tag/commit to install; VALHALLA_REF overrides the default main.
REF="${VALHALLA_REF:-main}"  # branch, tag or commit to fetch; overridable with VALHALLA_REF.
# Default install location under the user's home.
DEFAULT_DIR="$HOME/.local/share/vdt"  # the standard per-user data location on Linux and macOS.
# Install directory; VALHALLA_INSTALL_DIR overrides it (normalized below).
DIR="${VALHALLA_INSTALL_DIR:-$DEFAULT_DIR}"  # where files are installed; ${VALHALLA_INSTALL_DIR:-$DEFAULT_DIR} supplies the default.
# Directory holding the vdt link; VALHALLA_BIN_DIR overrides it.
BIN_DIR="${VALHALLA_BIN_DIR:-$HOME/.local/bin}"  # directory that will hold the 'vdt' command link.
# Non-empty VALHALLA_SOURCE means copy from a local checkout (used by tests).
SOURCE="${VALHALLA_SOURCE:-}"  # when set, copy files from a local checkout instead of downloading (used by tests).
# --no-rc sets this to 0 to leave shell rc files untouched.
EDIT_RC=1  # 1 = update shell rc files, 0 = leave them alone (--no-rc).
# Action selected by the CLI flags: install, uninstall or replace.
ACTION=install  # which operation to perform: install, uninstall or replace.

# Delimiters for the managed block written into shell rc files.
BEGIN_MARK="# >>> vdt >>>"  # start marker of the managed block written into rc files.
END_MARK="# <<< vdt <<<"  # end marker of that managed block.

# @brief Print a line to stdout.
# @param Text to print (arguments joined with spaces).
# @return Nothing.
say() { printf '%s\n' "$*"; }  # printf is used instead of echo for predictable output.
# @brief Print a red "error:" line to stderr and exit 1.
# @param Error message text.
# @return Does not return (exits with status 1).
die() { printf '%serror:%s %s\n' "$C_ERR" "$RST" "$*" >&2; exit 1; }  # print a red error line to stderr and exit with status 1.

# @brief Print this file's leading comment block as the usage/help text.
# @return Nothing; falls back to a hint if the header cannot be read.
usage() { sed -n '2,/^$/s/^# \{0,1\}//p' "$0" 2>/dev/null || say "See the header of install.sh in $REPO."; }  # sed prints the leading comment block (line 2 to the first blank line) as the help text; do not change that range.

while [ $# -gt 0 ]; do  # keep processing while command-line arguments remain.
  case "$1" in  # case matches the current option.
    --ref) [ $# -ge 2 ] || die "--ref needs a value"; REF="$2"; shift 2 ;;  # --ref needs a value; shift 2 consumes both the option and its value.
    --ref=*) REF="${1#*=}"; shift ;;  # the --ref=VALUE form strips the leading '--ref='.
    --dir) [ $# -ge 2 ] || die "--dir needs a value"; DIR="$2"; shift 2 ;;  # --dir sets the directory where files are installed.
    --dir=*) DIR="${1#*=}"; shift ;;  # the --dir=VALUE form strips the leading '--dir='.
    --bin-dir) [ $# -ge 2 ] || die "--bin-dir needs a value"; BIN_DIR="$2"; shift 2 ;;  # --bin-dir sets the directory that holds the command link.
    --bin-dir=*) BIN_DIR="${1#*=}"; shift ;;  # the --bin-dir=VALUE form strips the leading '--bin-dir='.
    --no-rc) EDIT_RC=0; shift ;;  # --no-rc disables editing of shell rc files.
    --uninstall) ACTION=uninstall; shift ;;  # --uninstall selects removal of the tool.
    --replace) ACTION=replace; shift ;;  # --replace selects download-first then swap.
    -h | --help) usage; exit 0 ;;  # --help prints the usage text and exits.
    *) die "unknown option: $1" ;;  # an unrecognized option is an error.
  esac
done

# absolute PATH: PATH made absolute (relative paths would break the link and
# the rc block), without a trailing slash.
# @brief Normalize a path to an absolute path with no trailing slash.
# @param PATH Absolute or relative path; relative paths resolve against $PWD.
# @return The normalized path printed on stdout.
absolute() {  # define a helper that turns a path into an absolute one.
  case "$1" in  # case: absolute paths already start with '/'.
    /*) printf '%s' "${1%/}" ;;  # ${1%/} removes one trailing slash, if present.
    *) printf '%s/%s' "$(pwd)" "${1%/}" ;;  # relative paths are joined onto the current directory.
  esac
}
# Normalize both directories once so later link/rc logic can rely on them.
DIR="$(absolute "$DIR")"  # normalize the install directory once, up front.
BIN_DIR="$(absolute "$BIN_DIR")"  # normalize the command directory once, up front.

# Files managed by this installer (relative to DIR). The leftover .sh names
# from earlier installs are removed on update.
MANAGED="vdt test_api schemathesis_test installer tests/api_tests/payloads api_tests/payloads docs/openapi.json VERSION update-state update-state.lock test_api.sh schemathesis_test.sh"  # space-separated list of every file this installer owns.

# ── rc files ───────────────────────────────────────────────────────────────
# rc_files: the rc files to manage: each of ~/.zshrc / ~/.bashrc that exists,
# plus the one for the login shell even if it doesn't exist yet.
# @brief List rc files to manage as "<shell> <path>" pairs.
# @return One line per rc file that should be updated.
rc_files() {  # list the rc files that should hold the PATH/completion block.
  login_shell="$(basename "${SHELL:-}")"  # basename strips the folder from $SHELL, leaving e.g. 'bash'.
  for sh_name in zsh bash; do  # consider zsh and bash in turn.
    rc="$HOME/.${sh_name}rc"  # rc is the config file for this shell.
    if [ -f "$rc" ] || [ "$login_shell" = "$sh_name" ]; then  # manage files that exist, or the login shell's file even if new.
      say "$sh_name $rc"  # print a 'shell path' line for the caller to process.
    fi
  done
  # macOS Terminal starts bash as a login shell, which reads ~/.bash_profile
  # and not ~/.bashrc.
  if [ "$(uname -s)" = Darwin ] && { [ "$login_shell" = bash ] || [ -f "$HOME/.bash_profile" ]; }; then  # macOS Terminal starts bash as a login shell, which reads .bash_profile.
    say "bash $HOME/.bash_profile"  # include .bash_profile in that case.
  fi
}

# strip_rc FILE: removes our block (between BEGIN_MARK and END_MARK).
# @brief Remove a previously written vdt block from an rc file, in place.
# @param FILE Rc file path (no-op if missing).
# @return Nothing; also collapses blank lines left at EOF.
strip_rc() {  # define a helper that removes a previously written block.
  [ -f "$1" ] || return 0  # there is nothing to clean if the file does not exist.
  # Build the result in a temp file, then copy back so the destination's
  # permissions and any symlink are preserved.
  tmp="$(mktemp)"  # build the new content in a temp file first.
  # awk drops everything between the two marker lines and removes blank lines left at the end.
  awk -v b="$BEGIN_MARK" -v e="$END_MARK" '
    index($0, b) == 1 { skip = 1; next }
    skip && index($0, e) == 1 { skip = 0; next }
    skip { next }
    /^[[:space:]]*$/ { blanks++; next }   # hold blank lines; drop them at EOF
    { while (blanks > 0) { print ""; blanks-- } print }' "$1" >"$tmp"
  cat "$tmp" >"$1" # keeps the file's permissions and any symlink
  rm -f "$tmp"  # remove the temp file after copying back.
}

# shell_path PATH: PATH written relative to $HOME when possible.
# @brief Render a path for shell rc files, using a literal $HOME prefix when possible.
# @param PATH Absolute path to render.
# @return A double-quoted shell expression printed on stdout.
shell_path() {  # define a helper that renders paths safely for rc files.
  # shellcheck disable=SC2016  # $HOME is meant to stay literal in the rc file
  case "$1" in
    "$HOME"/*) printf '"$HOME/%s"' "${1#"$HOME"/}" ;;  # paths under $HOME are written with a literal $HOME prefix.
    *) printf '"%s"' "$1" ;;  # other paths are written unchanged.
  esac
}

# @brief Replace the vdt PATH/completion block in a single rc file.
# @param sh_name Shell name (zsh/bash) used in the completion command.
# @param rc Rc file path to update.
# @return Nothing.
write_rc() {  # define the rc-block writer.
  sh_name="$1" rc="$2" d="$(shell_path "$DIR")" b="$(shell_path "$BIN_DIR")"  # compute the shell name, rc path and quoted directory paths in one go.
  strip_rc "$rc"  # remove any old block first so duplicates never accumulate.
  # Create the rc file if it does not exist yet.
  [ -f "$rc" ] || : >"$rc"  # create the rc file if it does not exist yet.
  # One blank line before the block, unless the file is empty or ends blank.
  if [ -s "$rc" ] && [ -n "$(tail -n 1 "$rc")" ]; then  # add a blank line first only if the file has content that does not already end blank.
    echo >>"$rc"  # write that blank line.
  fi
  cat >>"$rc" <<EOF  # append the managed block; this heredoc expands variables now.
$BEGIN_MARK (added by install.sh; remove this block to undo)
case ":\$PATH:" in *:$b:*) ;; *) export PATH=$b:"\$PATH" ;; esac
# vdt completion
[ -x $d/vdt ] && eval "\$($d/vdt completion $sh_name)"
$END_MARK
EOF
  say "📝  Updated $rc"  # tell the user which rc file was updated.
}

# ── download ───────────────────────────────────────────────────────────────
# @brief curl wrapper that adds a Bearer token when GITHUB_TOKEN is set.
# @param curl arguments (URL and options).
# @return curl's output and exit status.
api() {  # define a curl wrapper that can authenticate to GitHub.
  if [ -n "${GITHUB_TOKEN:-}" ]; then  # a token raises GitHub's low anonymous API rate limit.
    # A token raises GitHub's low anonymous API rate limit used by fetch_tree.
    curl -fsSL -H "Authorization: Bearer $GITHUB_TOKEN" "$@"  # send the token as a Bearer authorization header.
  else
    curl -fsSL "$@"  # without a token, call curl plainly.
  fi
}

# fetch_tree STAGE: downloads the files at commit $SHA into STAGE.
# @brief Resolve REF through the GitHub API and download the tool files into STAGE.
# @param STAGE Temporary directory to receive the files.
# @return Nothing; sets SHA, FILE_SHA and VERSION_TEXT.
fetch_tree() {  # resolve REF and download the tool files into the staging folder.
  stage="$1"  # stage is the first argument (a temporary directory).
  say "🔍  Resolving $REF in $REPO"  # tell the user what is being resolved.
  # The sha accept header makes the API return only the commit hash.
  SHA="$(api -H "Accept: application/vnd.github.sha" "https://api.github.com/repos/$REPO/commits/$REF")" ||  # ask GitHub which commit hash this ref points to.
    die "couldn't resolve '$REF' on GitHub (bad ref, no network, or API rate limit; set GITHUB_TOKEN to raise it)"  # fail with a helpful message if the lookup fails.
  case "$SHA" in *[!0-9a-f]* | "") die "unexpected answer from GitHub for '$REF': $SHA" ;; esac  # a valid commit hash is 40 hexadecimal characters.
  say "📥  Downloading $REF ($(printf '%s' "$SHA" | cut -c1-12))"  # show the first 12 characters of the hash.
  # The version we record is the commit that last touched valhalla.sh, not $SHA:
  # an update is offered only when the tool itself changes, not on every
  # unrelated commit to the branch.
  FILE_SHA="$(api "https://api.github.com/repos/$REPO/commits?path=scripts/vdt/valhalla.sh&sha=$SHA&per_page=1" |  # version is the commit that last changed valhalla.sh, not just any branch commit.
    sed -n 's/.*"sha": *"\([0-9a-f]\{40\}\)".*/\1/p' | head -n1)"  # sed extracts the first 40-character sha from GitHub's JSON reply.
  [ -n "$FILE_SHA" ] || FILE_SHA="$SHA"  # fall back to the branch hash when that lookup returns nothing.

# List payload paths from the Git tree so new payload files are picked up.
payloads="$(api "https://api.github.com/repos/$REPO/git/trees/$SHA?recursive=1" |  # list every payload JSON file from the git tree so new ones are picked up.
    grep -o '"path": *"tests/api_tests/payloads/[^"]*\.json"' |  # grep keeps 'path' entries that match the payloads folder.
    cut -d '"' -f 4)" ||  # cut extracts the path from inside the quotes.
    die "couldn't list the repository files (API rate limit? set GITHUB_TOKEN)"  # fail if the file list could not be fetched (often the API rate limit).

  count=0  # count how many files were downloaded.
  for f in scripts/vdt/valhalla.sh scripts/test/test_api.sh scripts/test/schemathesis_test.sh install.sh docs/openapi.json config $payloads; do  # loop over the fixed file list plus the discovered payloads.
    mkdir -p "$stage/$(dirname "$f")"  # create the destination folder for each file.
    # Raw downloads lose the executable bit; place_files restores it.
    curl -fsSL "https://raw.githubusercontent.com/$REPO/$SHA/$f" -o "$stage/$f" ||  # download the raw file; raw downloads lose the executable bit.
      die "download failed: $f (does $REF contain it?)"  # fail clearly if a file is missing from that ref.
    count=$((count + 1))  # count this file.
  done
  say "✅  Downloaded $count files"  # print the total number of files.
  VERSION_TEXT="$REF $FILE_SHA"  # record the version text used later in VERSION.
}

# copy_tree STAGE: copies the same files from a local checkout ($SOURCE).
# @brief Copy the tool files from the local $SOURCE checkout into STAGE.
# @param STAGE Temporary directory to receive the files.
# @return Nothing; sets VERSION_TEXT to "local <path> <git hash>".
copy_tree() {  # copy the same files from a local checkout instead of downloading.
  stage="$1"  # stage is the first argument.
  [ -f "$SOURCE/scripts/vdt/valhalla.sh" ] || die "VALHALLA_SOURCE has no valhalla.sh: $SOURCE"  # the checkout must contain valhalla.sh.
  say "📋  Copying from $SOURCE"  # tell the user where the files come from.
  for f in scripts/vdt/valhalla.sh scripts/test/test_api.sh scripts/test/schemathesis_test.sh install.sh docs/openapi.json config; do  # list the fixed set of files to copy.
    mkdir -p "$stage/$(dirname "$f")"
    cp "$SOURCE/$f" "$stage/$f"  # copy each file into the staging folder.
  done
  mkdir -p "$stage/tests/api_tests"  # create the payload destination folder.
  cp -R "$SOURCE/tests/api_tests/payloads" "$stage/tests/api_tests/payloads"  # copy the whole payloads folder.
  VERSION_TEXT="local $SOURCE $(git -C "$SOURCE" rev-parse HEAD 2>/dev/null || echo unknown)"  # record 'local <path> <git hash>' as the version text.
}

# ── default settings ───────────────────────────────────────────────────────
# write_default_env: creates DIR/.env with defaults for a small local
# container, unless there already is one (it's the user's file from then on).
# @brief Emit the bundled default config text on stdout (without a secret).
# @return Nothing.
default_env() {  # print the bundled default config text.
  cat <<'EOF'
# Settings for the vdt command (same format as a .env file): every
# variable here is passed to the server container. `vdt help` lists
# them all with their defaults. Created by install.sh and kept on update and
# uninstall, so edit freely. A .env in the directory you run vdt from
# is used instead of this file.
# Apply changes with: vdt update

# Small footprint for a local container (server defaults: 4 and 10)
SERVER_THREADS = 2
DB_MAX_CONN = 2

# Raised DoS limits so the API docs' Benchmark and Test console panels don't
# get your IP rate-limited (server defaults: 10000 and 100)
MAX_REQUESTS = 1000000
MAX_FPS = 3000000

# JWT signing secret, generated at install (keep it private; changing it
# logs everyone out)
EOF
}

# new_secret: 64 random hex characters.
# @brief Generate a 64-character random hex secret via openssl or /dev/urandom.
# @return The secret printed on stdout.
new_secret() {  # define the random-secret generator.
  if command -v openssl >/dev/null 2>&1; then  # prefer openssl when it is available.
    openssl rand -hex 32  # 32 random bytes become 64 hexadecimal characters.
  else
    od -An -tx1 -N32 /dev/urandom | tr -d ' \n'  # fallback: read 32 random bytes from /dev/urandom.
  fi
}

# has_secret FILE: does FILE set TOKEN_SECRET?
# @brief Check whether FILE already defines TOKEN_SECRET.
# @param FILE Config file path.
# @return 0 if defined, 1 otherwise.
has_secret() { grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$1"; }  # return success when the file already defines TOKEN_SECRET.
# @brief Hash FILE (sha256sum, shasum -a 256, or cksum fallback).
# @param FILE Path to hash.
# @return The hash string on stdout.
file_hash() {  # define a hashing helper used to spot user edits to the config.
  if command -v sha256sum >/dev/null 2>&1; then  # sha256sum is the usual choice on Linux.
    sha256sum "$1" | cut -d' ' -f1  # cut keeps just the hash, not the filename.
  elif command -v shasum >/dev/null 2>&1; then  # shasum -a 256 is the macOS equivalent.
    shasum -a 256 "$1" | cut -d' ' -f1  # cut keeps just the hash.
  else
    cksum "$1" | cut -d' ' -f1  # cksum is a weak but widely available last resort.
  fi
}

# @brief Create/repair $DIR/config with a generated TOKEN_SECRET or migrate a legacy .env.
# @return 0 always.
write_default_env() {  # create or repair the config file, including its secret.
  # Earlier versions kept the settings in .env; rename it.
  if [ -f "$DIR/.env" ] && [ ! -e "$DIR/config" ]; then  # older versions used .env; rename it when no config exists yet.
    mv "$DIR/.env" "$DIR/config"  # perform the rename.
    say "💾  Renamed $DIR/.env to $DIR/config"  # tell the user about the rename.
  fi
  if [ -e "$DIR/config" ]; then  # if a config exists, only repair it rather than replacing it.
    if ! has_secret "$DIR/config"; then  # add a secret if one is missing.
      printf '\n# JWT signing secret, generated at install (keep it private; changing it\n# logs everyone out)\nTOKEN_SECRET = "%s"\n' "$(new_secret)" >>"$DIR/config"  # append a fresh secret plus a short warning comment.
      say "🔑  Added a generated TOKEN_SECRET to $DIR/config"  # tell the user a secret was added.
    fi
    # The config holds the signing secret, so keep it owner-only.
    chmod 600 "$DIR/config"  # the config contains a secret, so only the owner may read it.
    return 0  # finished; skip the creation path below.
  fi
  # Restrict the new config to the owner from the moment it is created.
  (umask 077 && { default_env; printf 'TOKEN_SECRET = "%s"\n' "$(new_secret)"; } >"$DIR/config")  # create a new config atomically while the umask keeps it private.
  # The config holds the signing secret, so keep it owner-only.
  chmod 600 "$DIR/config"  # enforce private permissions again.
  say "📄  Created $DIR/config with default settings and a generated TOKEN_SECRET"  # tell the user a config was created.
}

# ── command link ───────────────────────────────────────────────────────────
# link_command: BIN_DIR/vdt -> DIR/vdt (never replaces a real file).
# @brief Symlink BIN_DIR/vdt to DIR/vdt unless a real file already occupies the path.
# @return Nothing.
link_command() {  # define the command-link helper.
  mkdir -p "$BIN_DIR"  # create the bin directory if needed.
  link="$BIN_DIR/vdt"  # full path of the link to create.
  if [ -e "$link" ] && [ ! -L "$link" ]; then  # do not overwrite a real file that the user placed there.
    say "🚧  warning: $link exists and isn't a link; left it alone. Run $DIR/vdt directly."  # warn and leave it untouched.
    return 0  # return without creating a link.
  fi
  # -n prevents following an existing symlink that points to a directory.
  ln -sfn "$DIR/vdt" "$link"  # -f forces replacement; -n avoids following a symlink that points to a folder.
  say "🔗  Linked $link -> $DIR/vdt"  # tell the user the link was made.
}

# unlink_command: removes BIN_DIR/vdt if it's our link.
# @brief Remove the vdt link only when it points at this install.
# @return Nothing.
unlink_command() {  # define the unlink helper.
  link="$BIN_DIR/vdt"  # full path of the link.
  if [ -L "$link" ] && [ "$(readlink "$link")" = "$DIR/vdt" ]; then  # only remove the link if it points at this installation.
    rm -f "$link"  # remove it.
    say "✂️   Removed $link"  # tell the user.
  fi
}

# remove_files DIR: deletes the managed files in DIR, and DIR if it's then
# empty; reports anything kept.
# @brief Delete managed files from a directory, then remove the directory if empty.
# @param target Directory to clean; user config/dumps are preserved where applicable.
# @return Nothing.
remove_files() {  # define the managed-file removal helper.
  target="$1"  # target is the first argument.
  # The :? expansion aborts if target is empty, avoiding an rm -rf of /.
  for f in $MANAGED; do rm -rf "${target:?}/$f"; done  # the :? expansion makes an empty target fatal, preventing 'rm -rf /'.
  # The default config goes too, unless it was edited.
  # (ignoring the TOKEN_SECRET line the installer generated). Not on
  # --replace: the config and its secret carry over to the new install.
  if [ "$ACTION" != replace ] && [ -f "$target/config" ]; then  # on uninstall (not replace), remove a default config the user never edited.
    if [ -f "$target/.config.default" ] && [ "$(file_hash "$target/config")" = "$(cat "$target/.config.default")" ]; then  # compare the saved hash with the current one.
      rm -f "$target/config"  # unmodified, so it is safe to remove.
    else
      # Compare a secret-free copy so a rotated secret is not mistaken for an edit.
      cmp_tmp="$(mktemp)"  # make a temporary copy with the secret removed.
      grep -vE '^TOKEN_SECRET = "[0-9a-f]{64}"$' "$target/config" >"$cmp_tmp" || true  # strip the generated secret line before comparing.
      if default_env | cmp -s - "$cmp_tmp"; then rm -f "$target/config"; fi  # compare the remaining text with the bundled default.
      rm -f "$cmp_tmp"  # remove the temporary comparison file.
    fi
  fi
  [ "$ACTION" = replace ] || rm -f "$target/.config.default"  # on --replace keep the saved hash so the new install can reuse it.
  # macOS Finder drops .DS_Store files into folders it shows; they'd keep
  # the folders from being removed.
  rm -f "$target/.DS_Store" "$target/api_tests/.DS_Store" "$target/docs/.DS_Store"  # remove Finder's .DS_Store files so folders can be emptied.
  rmdir "$target/tests/api_tests" "$target/tests" "$target/api_tests" "$target/docs" 2>/dev/null || true  # remove now-empty subfolders; ignore errors when they are not empty.
  if rmdir "$target" 2>/dev/null; then  # try to remove the main install directory.
    say "🗑   Removed $target"  # it was empty and is gone.
  else
    say "📤  Removed the tool from $target; kept your other files there:"  # it still contains user files, so report what was kept.
    for f in "$target"/* "$target"/.[!.]*; do  # list every remaining entry, including hidden ones.
      if [ -e "$f" ]; then say "  📁  $(basename "$f")"; fi # unmatched globs stay literal  # print each entry that still exists.
    done
  fi
}

# ── actions ────────────────────────────────────────────────────────────────
# stage_files: downloads (or copies) everything into a temp dir first.
# @brief Stage files in a temp dir and register cleanup on exit or signals.
# @return Nothing; sets the global stage path.
stage_files() {  # stage files before touching the current installation.
  command -v curl >/dev/null 2>&1 || die "curl is required"  # curl is required for downloads.
  stage="$(mktemp -d)"  # create a unique temporary staging directory.
  # Remove the staging tree on normal exit and on interrupt/termination.
  trap 'rm -rf "$stage"' EXIT INT TERM  # remove the staging tree on normal exit or interrupt.
  if [ -n "$SOURCE" ]; then copy_tree "$stage"; else fetch_tree "$stage"; fi  # choose a local copy or a download, depending on SOURCE.
}

# @brief Install: stage files, then copy them into place.
# @return Nothing.
do_install() {  # install means stage the files, then put them in place.
  stage_files  # first download or copy.
  place_files  # then install.
}

# do_replace: download first; only then remove the current copy and install.
# @brief Replace: stage first, then uninstall the old copy and install the new one.
# @return Nothing.
do_replace() {  # replace downloads first, so a failed download leaves the old install intact.
  stage_files  # download or copy first.
  say "🧹  Removing the current install in $DIR"  # announce the removal.
  do_uninstall  # remove the old copy.
  place_files  # install the new copy.
}

# @brief Install staged files, manage config/secret, link the command and update rc files.
# @return Nothing.
place_files() {  # install staged files, manage config, link the command and update rc files.
  # Replace the managed files only; config, dumps/ and anything else stay.
  mkdir -p "$DIR"  # create the install directory.
  config_preexisting=0  # remember whether the user already had a config.
  [ -e "$DIR/config" ] && config_preexisting=1  # 1 means a config already existed.
  # Clear only our managed files so unrelated user files survive the update.
  for f in $MANAGED; do rm -rf "${DIR:?}/$f"; done  # clear only the files this installer manages, so user files survive.
  # Installed without the .sh extension, under the tool's name.
  mv "$stage/scripts/vdt/valhalla.sh" "$stage/vdt"  # valhalla.sh becomes the 'vdt' command.
  mv "$stage/scripts/test/test_api.sh" "$stage/test_api"  # test_api.sh becomes 'test_api'.
  mv "$stage/scripts/test/schemathesis_test.sh" "$stage/schemathesis_test"  # schemathesis_test.sh becomes 'schemathesis_test'.
  mv "$stage/install.sh" "$stage/installer"  # this installer is kept as 'installer' so 'vdt uninstall' can call it.
  rm -rf "$stage/scripts"  # the scripts/ folder is no longer needed after renaming.
  [ "$config_preexisting" = 1 ] && rm -f "$stage/config"  # keep the user's config rather than the bundled one.
  (cd "$stage" && tar -cf - .) | (cd "$DIR" && tar -xf -)  # copy the whole staged tree into the install directory using tar.
  # The staged config is a bundled default, not the user's config.
  [ "$config_preexisting" = 1 ] || rm -f "$DIR/config"  # drop the bundled config when the user already had one.
  # Downloads arrive as plain files (curl doesn't keep the executable bit).
  chmod 755 "$DIR/vdt" "$DIR/test_api" "$DIR/schemathesis_test" "$DIR/installer"  # downloaded files are plain, so restore the executable bits.
  printf '%s\n' "$VERSION_TEXT" >"$DIR/VERSION"  # write the version string used by 'vdt version'.
  say "📦  Installed into $DIR"  # tell the user where the files were installed.

  # Migrate an old .env to config.
  if [ -f "$DIR/.env" ] && [ ! -e "$DIR/config" ]; then  # migrate an old .env file if one is present.
    mv "$DIR/.env" "$DIR/config"
    say "💾  Renamed $DIR/.env to $DIR/config"
  fi

  # Config: copy from the bundled default if missing or unmodified by the user.
  if [ ! -e "$DIR/config" ]; then  # create a config if none exists.
    if [ -f "$stage/config" ]; then  # use the bundled default when it is available.
      cp "$stage/config" "$DIR/config"  # copy it into place.
      # strip any bundled secret and generate a unique one
      if grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config"; then  # remove any secret that came with the bundle.
        grep -v -E '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config" >"$DIR/config.tmp" || true  # write the file without that secret line.
        mv "$DIR/config.tmp" "$DIR/config"  # swap the filtered copy into place.
      fi
      printf '\n# JWT signing secret, generated at install (keep it private; changing it\n# logs everyone out)\nTOKEN_SECRET = "%s"\n' "$(new_secret)" >>"$DIR/config"  # append a freshly generated secret.
      say "📄  Created $DIR/config from the bundled default with a generated TOKEN_SECRET"  # tell the user a config was created from the default.
    else
      # Restrict the new config to the owner from the moment it is created.
      (umask 077 && { default_env; printf 'TOKEN_SECRET = "%s"\n' "$(new_secret)"; } >"$DIR/config")  # create a private config when there is no bundled default.
      say "📄  Created $DIR/config with default settings and a generated TOKEN_SECRET"  # tell the user.
    fi
    # The config holds the signing secret, so keep it owner-only.
    chmod 600 "$DIR/config"  # lock down permissions.
    file_hash "$DIR/config" >"$DIR/.config.default"  # save the hash so a future update can detect user edits.
  elif [ -f "$DIR/.config.default" ] && [ "$(file_hash "$DIR/config")" = "$(cat "$DIR/.config.default")" ]; then  # if the config is still the untouched default, refresh it from the new bundle.
    # Unmodified default: refresh from the new bundle, preserving the user's secret.
    old_secret=""  # start with no saved secret.
    if grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config"; then  # look for an existing secret line.
      old_secret="$(grep -E '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config" | head -n1)"  # keep the first one found.
    fi
    if [ -f "$stage/config" ]; then  # only refresh when the new bundle contains a config.
      cp "$stage/config" "$DIR/config"  # replace the config with the new default.
      if [ -n "$old_secret" ]; then  # put the user's old secret back.
        grep -v -E '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config" >"$DIR/config.tmp" || true  # strip the bundled secret first.
        printf '%s\n' "$old_secret" >>"$DIR/config.tmp"  # append the saved secret.
        mv "$DIR/config.tmp" "$DIR/config"  # swap the assembled file into place.
      elif ! has_secret "$DIR/config"; then  # if there was no old secret, generate one now.
        printf '\nTOKEN_SECRET = "%s"\n' "$(new_secret)" >>"$DIR/config"  # append a fresh secret.
      fi
      file_hash "$DIR/config" >"$DIR/.config.default"  # update the saved hash.
      say "🔄  Refreshed the unmodified default config from the new bundle"  # tell the user the default was refreshed.
    fi
  else
    say "⚙️  Keeping your existing $DIR/config (user-modified)"  # the user edited the config, so leave it alone.
  fi
  # The config holds the signing secret, so keep it owner-only.
  chmod 600 "$DIR/config"  # enforce private permissions one last time.

  link_command  # create or repair the command link.

  if [ "$EDIT_RC" = 1 ]; then  # update rc files unless --no-rc was given.
    rc_files | while read -r sh_name rc; do write_rc "$sh_name" "$rc"; done  # feed each rc file to write_rc.
  fi

  say ""  # print a blank line.
  say "🎉  Done. Open a new terminal (or: source ~/.$(basename "${SHELL:-zsh}")rc), then:"  # tell the user to open a new terminal.
  say "  📖  vdt help"  # suggest the next command.
  say "  🚀  vdt run"  # suggest launching the server.
  say "⚙️  Settings: $DIR/config (a .env in the current directory wins)"  # point to the config file.
  say "🔄  Update later with: vdt install    ❌  Remove with: vdt uninstall"  # remind the user how to update or remove the tool.
}

# @brief Remove rc blocks, the command link and managed files (keeping user config/dumps).
# @return 0 always.
do_uninstall() {  # define the uninstall action.
  if [ "$EDIT_RC" = 1 ]; then  # clean rc files unless --no-rc was given.
    for rc in "$HOME/.zshrc" "$HOME/.bashrc" "$HOME/.bash_profile"; do  # check each of the three possible rc files.
      if [ -f "$rc" ] && grep -qF -e "$BEGIN_MARK" "$rc"; then  # only touch files that contain our marker.
        strip_rc "$rc"  # remove the managed block.
        say "🧽  Cleaned $rc"  # tell the user.
      fi
    done
  fi
  unlink_command  # remove the command link.
  if [ -d "$DIR" ]; then  # only clean up when the install directory exists.
    remove_files "$DIR"  # delete the managed files.
  else
    say "ℹ️  Nothing installed in $DIR"  # tell the user nothing was installed.
  fi
  return 0  # uninstall always reports success.
}

# Dispatch the action chosen by the CLI flags.
case "$ACTION" in  # dispatch to the action chosen by the flags.
  install) do_install ;;  # perform a normal install.
  uninstall) do_uninstall ;;  # perform an uninstall.
  replace) do_replace ;;  # perform a replace.
esac
