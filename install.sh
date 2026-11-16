#!/bin/sh
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

set -eu

# Red "error:" when stderr is a terminal.
if [ -t 2 ]; then C_ERR=$(printf '\033[31m'); RST=$(printf '\033[0m'); else C_ERR=; RST=; fi


REPO="dr3mro/Valhalla-server"
REF="${VALHALLA_REF:-main}"
DEFAULT_DIR="$HOME/.local/share/vdt"
DIR="${VALHALLA_INSTALL_DIR:-$DEFAULT_DIR}"
BIN_DIR="${VALHALLA_BIN_DIR:-$HOME/.local/bin}"
SOURCE="${VALHALLA_SOURCE:-}"
EDIT_RC=1
ACTION=install

BEGIN_MARK="# >>> vdt >>>"
END_MARK="# <<< vdt <<<"

say() { printf '%s\n' "$*"; }
die() { printf '%serror:%s %s\n' "$C_ERR" "$RST" "$*" >&2; exit 1; }

usage() { sed -n '2,/^$/s/^# \{0,1\}//p' "$0" 2>/dev/null || say "See the header of install.sh in $REPO."; }

while [ $# -gt 0 ]; do
  case "$1" in
    --ref) [ $# -ge 2 ] || die "--ref needs a value"; REF="$2"; shift 2 ;;
    --ref=*) REF="${1#*=}"; shift ;;
    --dir) [ $# -ge 2 ] || die "--dir needs a value"; DIR="$2"; shift 2 ;;
    --dir=*) DIR="${1#*=}"; shift ;;
    --bin-dir) [ $# -ge 2 ] || die "--bin-dir needs a value"; BIN_DIR="$2"; shift 2 ;;
    --bin-dir=*) BIN_DIR="${1#*=}"; shift ;;
    --no-rc) EDIT_RC=0; shift ;;
    --uninstall) ACTION=uninstall; shift ;;
    --replace) ACTION=replace; shift ;;
    -h | --help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

# absolute PATH: PATH made absolute (relative paths would break the link and
# the rc block), without a trailing slash.
absolute() {
  case "$1" in
    /*) printf '%s' "${1%/}" ;;
    *) printf '%s/%s' "$(pwd)" "${1%/}" ;;
  esac
}
DIR="$(absolute "$DIR")"
BIN_DIR="$(absolute "$BIN_DIR")"

# Files managed by this installer (relative to DIR). The leftover .sh names
# from earlier installs are removed on update.
MANAGED="vdt test_api schemathesis_test installer tests/api_tests/payloads api_tests/payloads docs/openapi.json VERSION update-state update-state.lock test_api.sh schemathesis_test.sh"

# ── rc files ───────────────────────────────────────────────────────────────
# rc_files: the rc files to manage: each of ~/.zshrc / ~/.bashrc that exists,
# plus the one for the login shell even if it doesn't exist yet.
rc_files() {
  login_shell="$(basename "${SHELL:-}")"
  for sh_name in zsh bash; do
    rc="$HOME/.${sh_name}rc"
    if [ -f "$rc" ] || [ "$login_shell" = "$sh_name" ]; then
      say "$sh_name $rc"
    fi
  done
  # macOS Terminal starts bash as a login shell, which reads ~/.bash_profile
  # and not ~/.bashrc.
  if [ "$(uname -s)" = Darwin ] && { [ "$login_shell" = bash ] || [ -f "$HOME/.bash_profile" ]; }; then
    say "bash $HOME/.bash_profile"
  fi
}

# strip_rc FILE: removes our block (between BEGIN_MARK and END_MARK).
strip_rc() {
  [ -f "$1" ] || return 0
  tmp="$(mktemp)"
  awk -v b="$BEGIN_MARK" -v e="$END_MARK" '
    index($0, b) == 1 { skip = 1; next }
    skip && index($0, e) == 1 { skip = 0; next }
    skip { next }
    /^[[:space:]]*$/ { blanks++; next }   # hold blank lines; drop them at EOF
    { while (blanks > 0) { print ""; blanks-- } print }' "$1" >"$tmp"
  cat "$tmp" >"$1" # keeps the file's permissions and any symlink
  rm -f "$tmp"
}

# shell_path PATH: PATH written relative to $HOME when possible.
shell_path() {
  # shellcheck disable=SC2016  # $HOME is meant to stay literal in the rc file
  case "$1" in
    "$HOME"/*) printf '"$HOME/%s"' "${1#"$HOME"/}" ;;
    *) printf '"%s"' "$1" ;;
  esac
}

write_rc() {
  sh_name="$1" rc="$2" d="$(shell_path "$DIR")" b="$(shell_path "$BIN_DIR")"
  strip_rc "$rc"
  [ -f "$rc" ] || : >"$rc"
  # One blank line before the block, unless the file is empty or ends blank.
  if [ -s "$rc" ] && [ -n "$(tail -n 1 "$rc")" ]; then
    echo >>"$rc"
  fi
  cat >>"$rc" <<EOF
$BEGIN_MARK (added by install.sh; remove this block to undo)
case ":\$PATH:" in *:$b:*) ;; *) export PATH=$b:"\$PATH" ;; esac
# vdt completion
[ -x $d/vdt ] && eval "\$($d/vdt completion $sh_name)"
$END_MARK
EOF
  say "📝  Updated $rc"
}

# ── download ───────────────────────────────────────────────────────────────
api() {
  if [ -n "${GITHUB_TOKEN:-}" ]; then
    curl -fsSL -H "Authorization: Bearer $GITHUB_TOKEN" "$@"
  else
    curl -fsSL "$@"
  fi
}

# fetch_tree STAGE: downloads the files at commit $SHA into STAGE.
fetch_tree() {
  stage="$1"
  say "🔍  Resolving $REF in $REPO"
  SHA="$(api -H "Accept: application/vnd.github.sha" "https://api.github.com/repos/$REPO/commits/$REF")" ||
    die "couldn't resolve '$REF' on GitHub (bad ref, no network, or API rate limit; set GITHUB_TOKEN to raise it)"
  case "$SHA" in *[!0-9a-f]* | "") die "unexpected answer from GitHub for '$REF': $SHA" ;; esac
  say "📥  Downloading $REF ($(printf '%s' "$SHA" | cut -c1-12))"
  # The version we record is the commit that last touched valhalla.sh, not $SHA:
  # an update is offered only when the tool itself changes, not on every
  # unrelated commit to the branch.
  FILE_SHA="$(api "https://api.github.com/repos/$REPO/commits?path=scripts/vdt/valhalla.sh&sha=$SHA&per_page=1" |
    sed -n 's/.*"sha": *"\([0-9a-f]\{40\}\)".*/\1/p' | head -n1)"
  [ -n "$FILE_SHA" ] || FILE_SHA="$SHA"

payloads="$(api "https://api.github.com/repos/$REPO/git/trees/$SHA?recursive=1" |
    grep -o '"path": *"tests/api_tests/payloads/[^"]*\.json"' |
    cut -d '"' -f 4)" ||
    die "couldn't list the repository files (API rate limit? set GITHUB_TOKEN)"

  count=0
  for f in scripts/vdt/valhalla.sh scripts/test/test_api.sh scripts/test/schemathesis_test.sh install.sh docs/openapi.json config $payloads; do
    mkdir -p "$stage/$(dirname "$f")"
    curl -fsSL "https://raw.githubusercontent.com/$REPO/$SHA/$f" -o "$stage/$f" ||
      die "download failed: $f (does $REF contain it?)"
    count=$((count + 1))
  done
  say "✅  Downloaded $count files"
  VERSION_TEXT="$REF $FILE_SHA"
}

# copy_tree STAGE: copies the same files from a local checkout ($SOURCE).
copy_tree() {
  stage="$1"
  [ -f "$SOURCE/scripts/vdt/valhalla.sh" ] || die "VALHALLA_SOURCE has no valhalla.sh: $SOURCE"
  say "📋  Copying from $SOURCE"
  for f in scripts/vdt/valhalla.sh scripts/test/test_api.sh scripts/test/schemathesis_test.sh install.sh docs/openapi.json config; do
    mkdir -p "$stage/$(dirname "$f")"
    cp "$SOURCE/$f" "$stage/$f"
  done
  mkdir -p "$stage/tests/api_tests"
  cp -R "$SOURCE/tests/api_tests/payloads" "$stage/tests/api_tests/payloads"
  VERSION_TEXT="local $SOURCE $(git -C "$SOURCE" rev-parse HEAD 2>/dev/null || echo unknown)"
}

# ── default settings ───────────────────────────────────────────────────────
# write_default_env: creates DIR/.env with defaults for a small local
# container, unless there already is one (it's the user's file from then on).
default_env() {
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
new_secret() {
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -hex 32
  else
    od -An -tx1 -N32 /dev/urandom | tr -d ' \n'
  fi
}

# has_secret FILE: does FILE set TOKEN_SECRET?
has_secret() { grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$1"; }
file_hash() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d' ' -f1
  else
    cksum "$1" | cut -d' ' -f1
  fi
}

write_default_env() {
  # Earlier versions kept the settings in .env; rename it.
  if [ -f "$DIR/.env" ] && [ ! -e "$DIR/config" ]; then
    mv "$DIR/.env" "$DIR/config"
    say "💾  Renamed $DIR/.env to $DIR/config"
  fi
  if [ -e "$DIR/config" ]; then
    if ! has_secret "$DIR/config"; then
      printf '\n# JWT signing secret, generated at install (keep it private; changing it\n# logs everyone out)\nTOKEN_SECRET = "%s"\n' "$(new_secret)" >>"$DIR/config"
      say "🔑  Added a generated TOKEN_SECRET to $DIR/config"
    fi
    chmod 600 "$DIR/config"
    return 0
  fi
  (umask 077 && { default_env; printf 'TOKEN_SECRET = "%s"\n' "$(new_secret)"; } >"$DIR/config")
  chmod 600 "$DIR/config"
  say "📄  Created $DIR/config with default settings and a generated TOKEN_SECRET"
}

# ── command link ───────────────────────────────────────────────────────────
# link_command: BIN_DIR/vdt -> DIR/vdt (never replaces a real file).
link_command() {
  mkdir -p "$BIN_DIR"
  link="$BIN_DIR/vdt"
  if [ -e "$link" ] && [ ! -L "$link" ]; then
    say "🚧  warning: $link exists and isn't a link; left it alone. Run $DIR/vdt directly."
    return 0
  fi
  ln -sfn "$DIR/vdt" "$link"
  say "🔗  Linked $link -> $DIR/vdt"
}

# unlink_command: removes BIN_DIR/vdt if it's our link.
unlink_command() {
  link="$BIN_DIR/vdt"
  if [ -L "$link" ] && [ "$(readlink "$link")" = "$DIR/vdt" ]; then
    rm -f "$link"
    say "✂️   Removed $link"
  fi
}

# remove_files DIR: deletes the managed files in DIR, and DIR if it's then
# empty; reports anything kept.
remove_files() {
  target="$1"
  for f in $MANAGED; do rm -rf "${target:?}/$f"; done
  # The default config goes too, unless it was edited.
  # (ignoring the TOKEN_SECRET line the installer generated). Not on
  # --replace: the config and its secret carry over to the new install.
  if [ "$ACTION" != replace ] && [ -f "$target/config" ]; then
    cmp_tmp="$(mktemp)"
    grep -vE '^TOKEN_SECRET = "[0-9a-f]{64}"$' "$target/config" >"$cmp_tmp" || true
    if default_env | cmp -s - "$cmp_tmp"; then rm -f "$target/config"; fi
    rm -f "$cmp_tmp"
  fi
  # macOS Finder drops .DS_Store files into folders it shows; they'd keep
  # the folders from being removed.
  rm -f "$target/.DS_Store" "$target/api_tests/.DS_Store" "$target/docs/.DS_Store"
  rmdir "$target/tests/api_tests" "$target/tests" "$target/api_tests" "$target/docs" 2>/dev/null || true
  if rmdir "$target" 2>/dev/null; then
    say "🗑   Removed $target"
  else
    say "📤  Removed the tool from $target; kept your other files there:"
    for f in "$target"/* "$target"/.[!.]*; do
      if [ -e "$f" ]; then say "  📁  $(basename "$f")"; fi # unmatched globs stay literal
    done
  fi
}

# ── actions ────────────────────────────────────────────────────────────────
# stage_files: downloads (or copies) everything into a temp dir first.
stage_files() {
  command -v curl >/dev/null 2>&1 || die "curl is required"
  stage="$(mktemp -d)"
  trap 'rm -rf "$stage"' EXIT INT TERM
  if [ -n "$SOURCE" ]; then copy_tree "$stage"; else fetch_tree "$stage"; fi
}

do_install() {
  stage_files
  place_files
}

# do_replace: download first; only then remove the current copy and install.
do_replace() {
  stage_files
  say "🧹  Removing the current install in $DIR"
  do_uninstall
  place_files
}

place_files() {
  # Replace the managed files only; config, dumps/ and anything else stay.
  mkdir -p "$DIR"
  config_preexisting=0
  [ -e "$DIR/config" ] && config_preexisting=1
  for f in $MANAGED; do rm -rf "${DIR:?}/$f"; done
  # Installed without the .sh extension, under the tool's name.
  mv "$stage/scripts/vdt/valhalla.sh" "$stage/vdt"
  mv "$stage/scripts/test/test_api.sh" "$stage/test_api"
  mv "$stage/scripts/test/schemathesis_test.sh" "$stage/schemathesis_test"
  mv "$stage/install.sh" "$stage/installer"
  (cd "$stage" && tar -cf - .) | (cd "$DIR" && tar -xf -)
  # The staged config is a bundled default, not the user's config.
  [ "$config_preexisting" = 1 ] || rm -f "$DIR/config"
  # Downloads arrive as plain files (curl doesn't keep the executable bit).
  chmod 755 "$DIR/vdt" "$DIR/test_api" "$DIR/schemathesis_test" "$DIR/installer"
  printf '%s\n' "$VERSION_TEXT" >"$DIR/VERSION"
  say "📦  Installed into $DIR"

  # Migrate an old .env to config.
  if [ -f "$DIR/.env" ] && [ ! -e "$DIR/config" ]; then
    mv "$DIR/.env" "$DIR/config"
    say "💾  Renamed $DIR/.env to $DIR/config"
  fi

  # Config: copy from the bundled default if missing or unmodified by the user.
  if [ ! -e "$DIR/config" ]; then
    if [ -f "$stage/config" ]; then
      cp "$stage/config" "$DIR/config"
      # strip any bundled secret and generate a unique one
      if grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config"; then
        grep -v -E '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config" >"$DIR/config.tmp" || true
        mv "$DIR/config.tmp" "$DIR/config"
      fi
      printf '\n# JWT signing secret, generated at install (keep it private; changing it\n# logs everyone out)\nTOKEN_SECRET = "%s"\n' "$(new_secret)" >>"$DIR/config"
      say "📄  Created $DIR/config from the bundled default with a generated TOKEN_SECRET"
    else
      (umask 077 && { default_env; printf 'TOKEN_SECRET = "%s"\n' "$(new_secret)"; } >"$DIR/config")
      say "📄  Created $DIR/config with default settings and a generated TOKEN_SECRET"
    fi
    chmod 600 "$DIR/config"
    file_hash "$DIR/config" >"$DIR/.config.default"
  elif [ -f "$DIR/.config.default" ] && [ "$(file_hash "$DIR/config")" = "$(cat "$DIR/.config.default")" ]; then
    # Unmodified default: refresh from the new bundle, preserving the user's secret.
    old_secret=""
    if grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config"; then
      old_secret="$(grep -E '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config" | head -n1)"
    fi
    if [ -f "$stage/config" ]; then
      cp "$stage/config" "$DIR/config"
      if [ -n "$old_secret" ]; then
        grep -v -E '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$DIR/config" >"$DIR/config.tmp" || true
        printf '%s\n' "$old_secret" >>"$DIR/config.tmp"
        mv "$DIR/config.tmp" "$DIR/config"
      elif ! has_secret "$DIR/config"; then
        printf '\nTOKEN_SECRET = "%s"\n' "$(new_secret)" >>"$DIR/config"
      fi
      file_hash "$DIR/config" >"$DIR/.config.default"
      say "🔄  Refreshed the unmodified default config from the new bundle"
    fi
  else
    say "⚙️  Keeping your existing $DIR/config (user-modified)"
  fi
  chmod 600 "$DIR/config"

  link_command

  if [ "$EDIT_RC" = 1 ]; then
    rc_files | while read -r sh_name rc; do write_rc "$sh_name" "$rc"; done
  fi

  say ""
  say "🎉  Done. Open a new terminal (or: source ~/.$(basename "${SHELL:-zsh}")rc), then:"
  say "  📖  vdt help"
  say "  🚀  vdt run"
  say "⚙️  Settings: $DIR/config (a .env in the current directory wins)"
  say "🔄  Update later with: vdt install    ❌  Remove with: vdt uninstall"
}

do_uninstall() {
  if [ "$EDIT_RC" = 1 ]; then
    for rc in "$HOME/.zshrc" "$HOME/.bashrc" "$HOME/.bash_profile"; do
      if [ -f "$rc" ] && grep -qF -e "$BEGIN_MARK" "$rc"; then
        strip_rc "$rc"
        say "🧽  Cleaned $rc"
      fi
    done
  fi
  unlink_command
  if [ -d "$DIR" ]; then
    remove_files "$DIR"
  else
    say "ℹ️  Nothing installed in $DIR"
  fi
  return 0
}

case "$ACTION" in
  install) do_install ;;
  uninstall) do_uninstall ;;
  replace) do_replace ;;
esac
