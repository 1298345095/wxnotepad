#!/usr/bin/env bash
# CI's real desktop-shell compositor, including the Weston #811 subsurface fix.
set -euo pipefail

source_dir=$(realpath "$1")
prefix=$(realpath -m "$2")
build_dir=$(realpath -m "${3:-.ci/weston-build}")

meson setup "$build_dir" "$source_dir" \
  --prefix="$prefix" --libdir=lib --buildtype=debugoptimized \
  "-Dc_link_args=-Wl,-rpath,$prefix/lib" \
  -Dbackend-drm=false -Dbackend-headless=false -Dbackend-pipewire=false \
  -Dbackend-rdp=false -Dbackend-vnc=false -Dbackend-wayland=false \
  -Dbackend-x11=true -Dbackend-default=x11 -Drenderer-gl=false \
  -Dxwayland=false -Dsystemd=false -Dremoting=false -Dpipewire=false \
  -Dscreenshare=false -Dshell-desktop=true -Dshell-fullscreen=false \
  -Dshell-ivi=false -Dshell-kiosk=false -Dcolor-management-lcms=false \
  -Dimage-jpeg=false -Dimage-webp=false -Dtools=debug \
  -Ddemo-clients=false -Dsimple-clients=[] -Dwcap-decode=false -Ddoc=false
meson compile -C "$build_dir" -j 2
meson install -C "$build_dir" --no-rebuild
"$prefix/bin/weston" --version
