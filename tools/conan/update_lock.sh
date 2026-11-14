#!/usr/bin/env bash
# Regenerates conan.lock for every platform we build: Alpine amd64/arm64 (Docker) and this machine.
# Usage: tools/conan/update_lock.sh   (ALPINE_GCC / ALPINE_CMAKE must match docker/Dockerfile's Alpine)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

ALPINE_GCC="${ALPINE_GCC:-15}"
ALPINE_CMAKE="${ALPINE_CMAKE:-4.2.3}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for arch in x86_64 armv8; do
	cat >"$tmp/linux-$arch" <<EOF
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
	conan lock create . --update --lockfile="" -pr:a "$tmp/linux-$arch" --lockfile-out="$tmp/linux-$arch.lock"
done

for build_type in Release Debug; do
	conan lock create . --update --lockfile="" -s build_type="$build_type" -s compiler.cppstd=gnu20 \
		--lockfile-out="$tmp/local-$build_type.lock"
done

args=()
for lock in "$tmp"/*.lock; do args+=(--lockfile="$lock"); done
conan lock merge "${args[@]}" --lockfile-out=conan.lock
echo "conan.lock updated. Commit it together with any conanfile.txt change."
