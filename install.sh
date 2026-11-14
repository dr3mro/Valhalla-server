#!/bin/sh
# Install the Valhalla tool straight from the GitHub repo: the `valhalla`
# command (valhalla.sh) as a link in ~/.local/bin, and its files (scripts
# without the .sh extension, test payloads, a copy of this installer) in
# ~/.local/share/valhalla. Adds ~/.local/bin to PATH if needed and tab
# completion in ~/.zshrc and/or ~/.bashrc.
#
#   curl -fsSL https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh | sh
#
# Re-run the same command, or `valhalla install`, to update to the latest version.
#
# Options (after `sh -s --` when piping, e.g. `| sh -s -- --uninstall`):
#   --ref REF       branch, tag or commit to install (default: main)
#   --dir DIR       where the files go     (default: ~/.local/share/valhalla)
#   --bin-dir DIR   where the command goes (default: ~/.local/bin)
#   --no-rc         don't touch ~/.zshrc / ~/.bashrc
#   --uninstall     remove the tool and the rc lines (keeps .env and dumps/);
#                   same as `valhalla uninstall`
#   -h, --help      show this help
#
# Environment: VALHALLA_REF, VALHALLA_INSTALL_DIR, VALHALLA_BIN_DIR,
# GITHUB_TOKEN (optional,
# avoids GitHub's anonymous API rate limit), VALHALLA_SOURCE (install from a
# local checkout instead of downloading; for testing).
#
# Installs: valhalla (valhalla.sh), test_api, schemathesis_test, installer
# (a copy of this script, used by `valhalla uninstall`), api_tests/payloads/,
# docs/openapi.json. Your .env and dumps/ in the install directory are never
# touched. An install in the old location (~/.bin/valhalla) is moved here,
# bringing its .env and dumps/ along.

set -eu

REPO="dr3mro/Valhalla-server"
REF="${VALHALLA_REF:-main}"
DEFAULT_DIR="$HOME/.local/share/valhalla"
DIR="${VALHALLA_INSTALL_DIR:-$DEFAULT_DIR}"
BIN_DIR="${VALHALLA_BIN_DIR:-$HOME/.local/bin}"
LEGACY_DIR="$HOME/.bin/valhalla"
SOURCE="${VALHALLA_SOURCE:-}"
EDIT_RC=1
ACTION=install

BEGIN_MARK="# >>> valhalla >>>"
END_MARK="# <<< valhalla <<<"

say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

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
    -h | --help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

# Older copies of `valhalla install` pass --dir with the old location.
DIR="${DIR%/}"
[ "$DIR" = "$LEGACY_DIR" ] && DIR="$DEFAULT_DIR"

# Files managed by this installer (relative to DIR).
# The .sh names are from earlier installs; they're removed on update.
MANAGED="valhalla test_api schemathesis_test installer api_tests/payloads docs/openapi.json VERSION deploy.sh test_api.sh schemathesis_test.sh"

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
}

# strip_rc FILE: removes our block and the line valhalla.sh adds on its own
# (also under its old name, deploy.sh).
strip_rc() {
  [ -f "$1" ] || return 0
  tmp="$(mktemp)"
  awk -v b="$BEGIN_MARK" -v e="$END_MARK" '
    index($0, b) == 1 { skip = 1; next }
    skip && index($0, e) == 1 { skip = 0; next }
    skip { next }
    /^# (deploy\.sh|valhalla) completion \(added by / { legacy = 1; next }
    legacy { legacy = 0; next }
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
# valhalla completion
[ -x $d/valhalla ] && eval "\$($d/valhalla completion $sh_name)"
$END_MARK
EOF
  say "Updated $rc"
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
  say "Resolving $REF in $REPO"
  SHA="$(api -H "Accept: application/vnd.github.sha" "https://api.github.com/repos/$REPO/commits/$REF")" ||
    die "couldn't resolve '$REF' on GitHub (bad ref, no network, or API rate limit; set GITHUB_TOKEN to raise it)"
  case "$SHA" in *[!0-9a-f]* | "") die "unexpected answer from GitHub for '$REF': $SHA" ;; esac
  say "Downloading $REF ($(printf '%s' "$SHA" | cut -c1-12))"

  payloads="$(api "https://api.github.com/repos/$REPO/git/trees/$SHA?recursive=1" |
    grep -o '"path": *"api_tests/payloads/[^"]*\.json"' |
    sed 's/.*"\(api_tests[^"]*\)"/\1/')" ||
    die "couldn't list the repository files (API rate limit? set GITHUB_TOKEN)"

  count=0
  for f in valhalla.sh test_api.sh schemathesis_test.sh install.sh docs/openapi.json $payloads; do
    mkdir -p "$stage/$(dirname "$f")"
    curl -fsSL "https://raw.githubusercontent.com/$REPO/$SHA/$f" -o "$stage/$f" ||
      die "download failed: $f (does $REF contain it?)"
    count=$((count + 1))
  done
  say "Downloaded $count files"
  VERSION_TEXT="$REF $SHA"
}

# copy_tree STAGE: copies the same files from a local checkout ($SOURCE).
copy_tree() {
  stage="$1"
  [ -f "$SOURCE/valhalla.sh" ] || die "VALHALLA_SOURCE has no valhalla.sh: $SOURCE"
  say "Copying from $SOURCE"
  for f in valhalla.sh test_api.sh schemathesis_test.sh install.sh docs/openapi.json; do
    mkdir -p "$stage/$(dirname "$f")"
    cp "$SOURCE/$f" "$stage/$f"
  done
  mkdir -p "$stage/api_tests"
  cp -R "$SOURCE/api_tests/payloads" "$stage/api_tests/payloads"
  VERSION_TEXT="local $SOURCE $(git -C "$SOURCE" rev-parse HEAD 2>/dev/null || echo unknown)"
}

# ── command link ───────────────────────────────────────────────────────────
# link_command: BIN_DIR/valhalla -> DIR/valhalla (never replaces a real file).
link_command() {
  mkdir -p "$BIN_DIR"
  link="$BIN_DIR/valhalla"
  if [ -e "$link" ] && [ ! -L "$link" ]; then
    say "warning: $link exists and isn't a link; left it alone. Run $DIR/valhalla directly."
    return 0
  fi
  ln -sfn "$DIR/valhalla" "$link"
  say "Linked $link -> $DIR/valhalla"
}

# unlink_command: removes BIN_DIR/valhalla if it's our link.
unlink_command() {
  link="$BIN_DIR/valhalla"
  if [ -L "$link" ] && [ "$(readlink "$link")" = "$DIR/valhalla" ]; then
    rm -f "$link"
    say "Removed $link"
  fi
}

# remove_files DIR: deletes the managed files in DIR, and DIR if it's then
# empty; reports anything kept.
remove_files() {
  target="$1"
  for f in $MANAGED; do rm -rf "${target:?}/$f"; done
  # macOS Finder drops .DS_Store files into folders it shows; they'd keep
  # the folders from being removed.
  rm -f "$target/.DS_Store" "$target/api_tests/.DS_Store" "$target/docs/.DS_Store"
  rmdir "$target/api_tests" "$target/docs" 2>/dev/null || true
  if rmdir "$target" 2>/dev/null; then
    say "Removed $target"
  else
    say "Removed the tool from $target; kept your other files there:"
    for f in "$target"/* "$target"/.[!.]*; do [ -e "$f" ] && say "  $(basename "$f")"; done
  fi
}

# migrate_legacy: moves an install from ~/.bin/valhalla to DIR, bringing
# .env and dumps/ along (unless DIR already has its own).
migrate_legacy() {
  [ "$DIR" != "$LEGACY_DIR" ] || return 0
  [ -f "$LEGACY_DIR/VERSION" ] || [ -e "$LEGACY_DIR/valhalla" ] || return 0
  say "Moving the old install from $LEGACY_DIR to $DIR"
  mkdir -p "$DIR"
  for f in .env dumps; do
    if [ -e "$LEGACY_DIR/$f" ] && [ ! -e "$DIR/$f" ]; then
      mv "$LEGACY_DIR/$f" "$DIR/$f"
      say "  moved $f"
    fi
  done
  remove_files "$LEGACY_DIR"
  rmdir "$HOME/.bin" 2>/dev/null || true
}

# ── actions ────────────────────────────────────────────────────────────────
do_install() {
  command -v curl >/dev/null 2>&1 || die "curl is required"

  stage="$(mktemp -d)"
  trap 'rm -rf "$stage"' EXIT INT TERM
  if [ -n "$SOURCE" ]; then copy_tree "$stage"; else fetch_tree "$stage"; fi

  migrate_legacy

  # Replace the managed files only; .env, dumps/ and anything else stay.
  mkdir -p "$DIR"
  for f in $MANAGED; do rm -rf "${DIR:?}/$f"; done
  # Installed without the .sh extension.
  mv "$stage/valhalla.sh" "$stage/valhalla"
  mv "$stage/test_api.sh" "$stage/test_api"
  mv "$stage/schemathesis_test.sh" "$stage/schemathesis_test"
  mv "$stage/install.sh" "$stage/installer"
  (cd "$stage" && tar -cf - .) | (cd "$DIR" && tar -xf -)
  # Downloads arrive as plain files (curl doesn't keep the executable bit).
  chmod 755 "$DIR/valhalla" "$DIR/test_api" "$DIR/schemathesis_test" "$DIR/installer"
  printf '%s\n' "$VERSION_TEXT" >"$DIR/VERSION"
  say "Installed into $DIR"
  link_command

  if [ "$EDIT_RC" = 1 ]; then
    rc_files | while read -r sh_name rc; do write_rc "$sh_name" "$rc"; done
  fi

  say ""
  say "Done. Open a new terminal (or: source ~/.$(basename "${SHELL:-zsh}")rc), then:"
  say "  valhalla help"
  say "  valhalla run"
  [ -f "$DIR/.env" ] || say "Settings: valhalla reads .env from the current directory, else $DIR/.env"
  say "Update later with: valhalla install    Remove with: valhalla uninstall"
}

do_uninstall() {
  if [ "$EDIT_RC" = 1 ]; then
    for rc in "$HOME/.zshrc" "$HOME/.bashrc"; do
      if [ -f "$rc" ] && grep -qF -e "$BEGIN_MARK" -e "# deploy.sh completion (added by" -e "# valhalla completion (added by" "$rc"; then
        strip_rc "$rc"
        say "Cleaned $rc"
      fi
    done
  fi
  unlink_command
  if [ -d "$DIR" ]; then
    remove_files "$DIR"
  else
    say "Nothing installed in $DIR"
  fi
  # Also clean up an install in the old location.
  if [ "$DIR" != "$LEGACY_DIR" ] && { [ -f "$LEGACY_DIR/VERSION" ] || [ -e "$LEGACY_DIR/valhalla" ]; }; then
    remove_files "$LEGACY_DIR"
    rmdir "$HOME/.bin" 2>/dev/null || true
  fi
}

case "$ACTION" in
  install) do_install ;;
  uninstall) do_uninstall ;;
esac
