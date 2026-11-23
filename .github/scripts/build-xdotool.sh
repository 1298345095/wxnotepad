#!/usr/bin/env bash
set -euo pipefail
source_dir=$(realpath "${1:?source directory required}")
prefix=$(realpath -m "${2:?installation directory required}")
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
# Upstream's static target embeds libxdo while retaining native X11/XTEST input.
make -C "$source_dir" -j "$jobs" static
make -C "$source_dir" PREFIX="$prefix" install-static
[[ "$("$prefix/bin/xdotool" --version)" == 'xdotool version 4.20260303.1' ]]
