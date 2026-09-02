#!/usr/bin/env bash
set -euo pipefail

image="${1:-local/omarchy-full:test}"

docker image inspect "$image" >/dev/null

docker run --rm --entrypoint /usr/bin/bash "$image" -lc '
set -euo pipefail

pacman -Q \
  omarchy omarchy-settings \
  chromium nautilus libreoffice-fresh kdenlive obs-studio \
  clang llvm

for command in chromium nautilus libreoffice kdenlive obs; do
  command -v "$command" >/dev/null || {
    echo "missing full-image command: $command" >&2
    exit 1
  }
done

if pacman -Qq | grep -Eq "^(docker|docker-buildx|docker-compose|networkmanager|sddm|ufw)$"; then
  echo "full image contains an excluded host service" >&2
  exit 1
fi

echo "full image smoke checks passed"
'
