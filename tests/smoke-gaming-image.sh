#!/usr/bin/env bash
set -euo pipefail

image="${1:-local/omarchy-gaming:test}"

docker image inspect "$image" >/dev/null

docker run --rm --entrypoint /usr/bin/bash "$image" -lc '
set -euo pipefail

pacman -Q \
  steam umu-launcher gamescope \
  mangohud lib32-mangohud \
  gamemode lib32-gamemode \
  omarchy-container-gpu-runtime

pacman -T
id -nG omarchy | grep -qw gamemode
test ! -e /etc/security/limits.d/10-gamemode.conf

for command in steam umu-run gamescope mangohud gamemoderun; do
  command -v "$command" >/dev/null || {
    echo "missing gaming command: $command" >&2
    exit 1
  }
done

pacman -Qi omarchy-container-gpu-runtime | \
  grep -F "Provides        : vulkan-driver  lib32-vulkan-driver"

if pacman -Qq | grep -Eq "^(nvidia-utils|lib32-nvidia|vulkan-asahi)$"; then
  echo "gaming image contains an unexpected baked vendor driver" >&2
  exit 1
fi

echo "gaming image smoke checks passed"
'
