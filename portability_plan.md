# Shell script portability audit (Linux / macOS / Windows)

Reviewed every hand-written `.sh` in the repo. The `build/**/*.sh` files are
Conan-generated and git-ignored, so they are excluded.

Tooling used: `shellcheck` (bash and `-s sh`), `dash -n`, plus behaviour probes
on macOS 26 with `/bin/bash` 3.2.57 and the BSD userland.

## Verdict

- **Linux: solid.**
- **macOS: mostly fine** on current versions.
- **Windows: not really supported.** Not because of bash syntax (that is
  careful), but because of POSIX assumptions (symlinks, path translation,
  `open`, `python3`) and the missing `.gitattributes`. Native MSVC builds also
  break on `get_tag.sh` / `server.exe`.

Already done well:

- `#!/usr/bin/env bash` everywhere except `build.sh`.
- bash 3.2 compatibility (no associative arrays, `mapfile`, `${x,,}`).
- CRLF-tolerant env parsing (`${line%$'\r'}`).
- No `sed -i` / `grep -P` / `readlink -f` / `stat -c` / `date -d` on the host.
- `command -v` everywhere, `mktemp` templates.
- `--env-file` instead of `-e`.
- `install.sh` / `docker/entrypoint.sh` are POSIX-clean (`dash -n` and
  `shellcheck -s sh` pass).

---

## High impact

| # | File / lines | Problem | Affects |
|---|---|---|---|
| 1 | repo root (no `.gitattributes`) | Git for Windows defaults to `core.autocrlf=true`; `.sh`, `Dockerfile`, `entrypoint.sh` get CRLF => `bad interpreter: /usr/bin/env bash^M`, and `docker/entrypoint.sh` fails inside the image | Windows |
| 2 | `install.sh:249-267` | `ln -sfn` + `[ -L ]` / `readlink`. Git Bash usually copies instead of symlinking, so `link_command` reports "Linked" but creates a stale copy, and `unlink_command` never removes it. Re-running the installer then hits the "exists and isn't a link; left it alone" branch forever | Windows |
| 3 | `valhalla.sh:1100, 2452-2464` | `doc` needs `open` or `xdg-open`; Windows Git Bash has neither (minimal WSL may not either) => command always fails | Windows, WSL |
| 4 | `valhalla.sh:1653-1657` | `-v "$DUMP_DIR:/persist"` — MSYS path translation turns `/c/Users/...` into `C:\Users\...`, and Docker reads the `C:` colon as the volume separator. Persistence mounts break under Git Bash | Windows |
| 5 | `valhalla.sh:1263-1272, 2430, 2443` | `docker exec -it` and `docker logs -f` / `docker stats` under mintty: `[[ -t 0 ]]` is true but the CLI errors "the input device is not a TTY"; needs `winpty` or native-Windows handling | Windows |
| 6 | `schemathesis_test.sh:47, 58-62` | `pip3 install --user` fails under PEP 668 (Debian 12+, Ubuntu 23.04+, Fedora, Homebrew Python on macOS), and `~/.local/bin` is the wrong location on Windows (`%APPDATA%\Python\...\Scripts`). `tools/docs/build.sh:22-27` already solves this with a venv — this script doesn't | macOS, Windows, newer Linux |
| 7 | `fuzz_auth.sh:13`, `sqlmap_test.sh:62`, `schemathesis_test.sh:55`, `tools/docs/build.sh:20`, `valhalla.sh:1086,1092,1108` | Hard requirement on `python3`; Windows normally exposes `python` / `py`, and stock macOS ships only the Xcode shim | Windows, macOS |
| 8 | `tools/docs/build.sh:24-26` | `.venv/bin/python` — Windows venvs put the interpreter in `.venv/Scripts/python.exe` | Windows |
| 9 | `tools/cmake/gen_version.cmake:10-13` -> `get_tag.sh` | `execute_process(COMMAND "${SOURCE_DIR}/get_tag.sh")` cannot run a `.sh` file under native Windows CMake (and fails wherever bash isn't on PATH); the result silently becomes `"unknown"` | Windows native |
| 10 | `run_release.sh:11` | `BIN="$ROOT/build/Release/server"`; the Windows binary is `server.exe` => "not found, run ./build.sh" | Windows native |
| 11 | `db_init.sh:1` | No shebang, no `set -e`, and `init.sql` is resolved from the current directory, not the script's directory. Direct execution (e.g. from Python `subprocess`) fails with Exec format error | all |
| 12 | `valhalla.sh:1092` | `outdated` hard-requires `python3`, but `registry_digest` doesn't use it — only the optional Hub-tag lookup at 1947 does. Fails on a machine without python3 for no reason | all |

### Suggested fixes (top items)

```sh
# .gitattributes (new)
* text=auto
*.sh           text eol=lf
Dockerfile     text eol=lf
*.cmake        text eol=lf
```

```sh
# install.sh — link_command: fall back to a copy where symlinks aren't real
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) cp -f "$DIR/vdt" "$link" ;;   # or MSYS=winsymlinks:nativestrict
  *) ln -sfn "$DIR/vdt" "$link" ;;
esac
```

```sh
# valhalla.sh — cmd_doc: add Windows/WSL openers
if   command -v open     >/dev/null; then open "$DOC_URL"
elif command -v xdg-open >/dev/null; then xdg-open "$DOC_URL"
elif command -v wslview  >/dev/null; then wslview "$DOC_URL"
elif command -v cmd.exe  >/dev/null; then cmd.exe /c start "" "$DOC_URL"
elif command -v powershell.exe >/dev/null; then powershell.exe -NoProfile Start-Process "$DOC_URL"
fi
```

```sh
# valhalla.sh — create_container: don't let MSYS mangle the bind mount
mount_src="$DUMP_DIR"
command -v cygpath >/dev/null 2>&1 && mount_src="$(cygpath -w "$DUMP_DIR")"
args+=(-v "$mount_src:/persist")
```

```cmake
# gen_version.cmake caller
COMMAND bash "${SOURCE_DIR}/get_tag.sh"
```

```sh
# run_release.sh
BIN="$ROOT/build/Release/server"
[[ -x $BIN ]] || BIN="$ROOT/build/Release/server.exe"
```

```sh
# docs build / schemathesis: pick the venv interpreter per OS
PY_BIN=".venv/bin/python"; [[ -x .venv/Scripts/python.exe ]] && PY_BIN=".venv/Scripts/python.exe"
```

---

## macOS-specific

- `valhalla.sh:1906` — `find "$lock" -maxdepth 0 -mmin +2` is GNU-style; it
  works on current macOS 26 (verified) but not on older BSD `find`, and
  `2>/dev/null` swallows the failure so a stale lock is never cleared. The
  directory is empty, so `find "$lock" -mmin +2` (no `-maxdepth`) is equally
  correct and portable.
- `docker/entrypoint.sh:105` — `stat -c '%u:%g'` is GNU-only. It runs in
  Alpine/BusyBox today (works) with a `|| echo 0:0` fallback, so this only
  bites if the base image changes to Debian or the entrypoint is tested on
  macOS. Add a `stat -f '%u:%g'` fallback for completeness.
- `valhalla.sh:2385` — `date -r "$pf"` is fine: macOS `date -r` accepts a
  filename as well as seconds. No change needed. (Same for `sort -V` — BSD
  sort on macOS supports it.)
- `valhalla.sh:943, 1000, 1013` — `printf '%q'` is bash-only and, on bash 3.2,
  escapes non-ASCII bytes; embedding the result in a generated **zsh**
  completion/rc line can misparse if the install path contains spaces or
  non-ASCII. Low risk, worth quoting differently for zsh.
- `valhalla.sh:617` — dead duplicate assignment:
  `EG = M ? "   " : " "; EG = M ? "  " : " "` (harmless, but confusing).

## Linux / general

- `build.sh:1` — `#!/bin/bash` breaks on NixOS/FreeBSD/Alpine-without-bash;
  change to `#!/usr/bin/env bash`. It also has no `set -euo pipefail`, so a
  failed `conan`/`cmake` still runs `ctest` and starts a stale binary.
- `test_api.sh:49`, `schemathesis_test.sh:49`, `sqlmap_test.sh:69` — `✓`/`✗`
  and box glyphs are gated only on `-t 1`, not on a UTF-8 locale; Windows
  terminals using cp1252 will show mojibake. `valhalla.sh`'s `fancy_level`
  already does this correctly — reuse that logic.
- `test_api.sh:25`, `valhalla.sh:2598` — `date +%s | tail -c 7` is portable
  but truncates to 6 digits, so two runs in the same second collide.
  `${EPOCHSECONDS:-$(date +%s)}` isn't 3.2-safe; just append `$$` / `$RANDOM`
  instead.
- `test_api.sh:21` etc. — `${BASH_SOURCE[0]}` breaks if someone runs
  `sh test_api.sh`. Not an issue when the shebang is honored.
- `docker/entrypoint.sh` is committed mode `100644`; the Dockerfile masks that
  with `COPY --chmod=755`, but a compose/bind-mount invocation would need the
  exec bit. Set it in git for safety.
- `tools/conan/update_lock.sh` — fine on Linux/macOS/Git Bash; nothing to
  change.
- `shellcheck` output is clean apart from three non-portability warnings in
  `test_api.sh` (`ID`, `LOGIN_USERNAME`, `sid`/`stok` — all set dynamically
  via `printf -v`, so false positives).

## Testing gap

`.github/workflows/scripts.yml` runs `shellcheck` + `dash -n` + the
installer/Docker tests only on `ubuntu-24.04`. That is why items 1–9 are
invisible today. A `macos-14` job (bash 3.2 + BSD userland) and a Git Bash
smoke test (`sh install.sh`, `valhalla.sh help`, `completion bash`) would catch
most of this cheaply.

---

## Decisions to confirm before implementing

1. Which Windows target matters: **Git Bash**, **WSL**, or **native MSVC**
   (or a mix)? That decides whether we fix path translation with `cygpath`,
   or only document WSL as the supported route.
2. Scope: fix everything above, or only the High-impact set (items 1–12)?
