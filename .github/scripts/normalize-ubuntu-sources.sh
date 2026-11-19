#!/usr/bin/env bash
# Select the official Ubuntu archive in the runner's source files.
set -euo pipefail

for ubuntu_source in "$@"; do
  if [[ -f "$ubuntu_source" ]]; then
    # New runner images use this mirror list instead of a direct Azure URI.
    sed -i -E \
      -e 's@https?://azure\.archive\.ubuntu\.com/ubuntu@https://archive.ubuntu.com/ubuntu@g' \
      -e 's@mirror\+file:/etc/apt/apt-mirrors\.txt([/[:space:]]|$)@https://archive.ubuntu.com/ubuntu\1@g' \
      "$ubuntu_source"
  fi
done
