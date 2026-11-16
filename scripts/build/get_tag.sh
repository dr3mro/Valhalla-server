#!/usr/bin/env bash
# /**
#  * @file get_tag.sh
#  * @brief Emit a -DGIT_TAG compiler macro from the explicit VALHALLA_VERSION or the closest Git tag, for version stamping.
#  * @details Print a compiler -D flag that stamps the current Git tag (or an explicit version) into the compiled binary.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * CMake calls this script to give the server a version string it can report at runtime.
#  * Docker builds have no .git folder, so they pass VALHALLA_VERSION instead.
#  * Without it, the server could not tell users which release is running.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - git describe: https://git-scm.com/docs/git-describe
#  * - Bash conditionals: https://www.gnu.org/software/bash/manual/bash.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */

# Builds without .git (e.g. Docker) pass the version explicitly
if [[ -n ${VALHALLA_VERSION:-} ]]; then  # VALHALLA_VERSION is supplied by builds that do not have Git available (e.g. Docker).
	# The `:-` default keeps this check safe if -u is inherited; short-circuit since Git is unavailable/unneeded.
	echo "-DGIT_TAG=\"${VALHALLA_VERSION}\""  # echo prints the macro text that CMake will add to the compiler command line.
	exit 0  # exit 0 means success; there is no need to call Git in this case.
fi

# Try to get the exact tag
# Suppress Git errors so a missing repo or tag falls through to the next strategy instead of failing.
current_tag=$(git describe --tags --exact-match 2>/dev/null)  # 2>/dev/null hides Git's error so the fallback strategies below still run.

# If the exact tag is not found, get the closest tag
if [[ -z ${current_tag} ]]; then  # -z tests for an empty string, meaning the exact-tag lookup found nothing.
	current_tag=$(git describe --tags 2>/dev/null)  # 'Closest tag' is the newest tag reachable from the current commit.
fi

# If no tag is found, list all tags pointing to the current commit
if [[ -z ${current_tag} ]]; then  # Last resort: look for tags that point directly at this commit.
	current_tag=$(git tag --points-at HEAD)  # --points-at HEAD lists only tags attached to the current commit.
fi

# Output the current tag in a format suitable for a macro
# Escaped quotes make the value a C/C++ string literal when the macro is expanded by the compiler.
echo "-DGIT_TAG=\"${current_tag}\""  # The escaped quotes make the value a proper C/C++ string literal for the macro.
