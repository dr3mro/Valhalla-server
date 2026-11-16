#!/usr/bin/env bash
# /**
#  * @file update_lock.sh
#  * @brief Regenerates the committed conan.lock: creates fresh lockfiles for Alpine (Docker) amd64/arm64 and for the host (Release/Debug), then merges them into one file.
#  * @details Regenerate conan.lock by creating separate lockfiles for Alpine Linux amd64/arm64 and for the host Release/Debug builds, then merging them.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * A lockfile pins exact dependency versions so Docker and local builds reproduce identically.
#  * It is run manually after changing conanfile.txt or upgrading the toolchain.
#  * Without it, builds could silently pick different package revisions and behave differently.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Conan lockfiles: https://docs.conan.io/2/
#  * - mktemp: https://www.gnu.org/software/coreutils/manual/html_node/mktemp-invocation.html
#  * - Bash trap: https://www.gnu.org/software/bash/manual/html_node/Bourne-Shell-Builtins.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Regenerates conan.lock for every platform we build: Alpine amd64/arm64 (Docker) and this machine.
# Usage: tools/conan/update_lock.sh   (ALPINE_GCC / ALPINE_CMAKE must match docker/Dockerfile's Alpine)
set -euo pipefail  # e = stop on error, u = error on unset variable, pipefail = catch pipeline failures.
# Anchor to the repo root so the "." given to conan and the output conan.lock resolve the same way regardless of caller cwd.
cd "$(dirname "${BASH_SOURCE[0]}")/../.."  # BASH_SOURCE[0] is this script; ../.. climbs to the repository root.

# Keep these in sync with the Alpine base image's toolchain; overridable via env for other images.
ALPINE_GCC="${ALPINE_GCC:-15}"  # GCC version used by the Alpine image; can be overridden via the environment.
ALPINE_CMAKE="${ALPINE_CMAKE:-4.2.3}"  # CMake version used to build dependencies in that image.

# Per-target lockfiles live in a scratch dir; the trap removes them so only the merged conan.lock is committed.
tmp="$(mktemp -d)"  # mktemp -d creates a uniquely named scratch directory.
trap 'rm -rf "$tmp"' EXIT  # The trap deletes the scratch directory on EXIT, even if the script fails.

# armv8 is Conan's arch token for 64-bit ARM (aarch64).
for arch in x86_64 armv8; do  # x86_64 means Intel/AMD 64-bit; armv8 is Conan's name for 64-bit ARM.
	# Minimal profile per Alpine target; platform_tool_requires pins the CMake used to build dependencies.
	cat >"$tmp/linux-$arch" <<EOF  # Write a minimal Conan profile for this target into the scratch directory.
[settings]
os=Linux
arch=$arch
compiler=gcc
compiler.version=$ALPINE_GCC
compiler.libcxx=libstdc++11
compiler.cppstd=gnu20
build_type=Release
[platform_tool_requires]
cmake/$ALPINE_CMAKE
EOF
	# --lockfile="" ignores the existing conan.lock so newer revisions can be picked up
	conan lock create . --update --lockfile="" -pr:a "$tmp/linux-$arch" --lockfile-out="$tmp/linux-$arch.lock"  # --update fetches newest revisions; --lockfile="" ignores the existing lock.
done

# Also lock the host builds using the default local profile; cppstd is pinned to match the project.
for build_type in Release Debug; do  # Also lock the two host builds using this machine's default profile.
	conan lock create . --update --lockfile="" -s build_type="$build_type" -s compiler.cppstd=gnu20 \
		--lockfile-out="$tmp/local-$build_type.lock"  # Write this target's lockfile into the scratch directory.
done

# Merge every per-target lockfile into the single conan.lock consumed by conan install / Docker builds.
args=()  # Start with an empty list of merge arguments.
for lock in "$tmp"/*.lock; do args+=(--lockfile="$lock"); done  # Add one --lockfile=... argument for each generated lockfile.
conan lock merge "${args[@]}" --lockfile-out=conan.lock  # Merge everything into the single conan.lock used by conan install and Docker.
echo "conan.lock updated. Commit it together with any conanfile.txt change."  # Remind the developer to commit the updated lockfile.
