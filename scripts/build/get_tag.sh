#!/usr/bin/env bash

# Builds without .git (e.g. Docker) pass the version explicitly
if [[ -n ${VALHALLA_VERSION:-} ]]; then
	echo "-DGIT_TAG=\"${VALHALLA_VERSION}\""
	exit 0
fi

# Try to get the exact tag
current_tag=$(git describe --tags --exact-match 2>/dev/null)

# If the exact tag is not found, get the closest tag
if [[ -z ${current_tag} ]]; then
	current_tag=$(git describe --tags 2>/dev/null)
fi

# If no tag is found, list all tags pointing to the current commit
if [[ -z ${current_tag} ]]; then
	current_tag=$(git tag --points-at HEAD)
fi

# Output the current tag in a format suitable for a macro
echo "-DGIT_TAG=\"${current_tag}\""
