#!/usr/bin/env bash
set -euo pipefail

image="${1:-local/omarchy-gaming:test}"

docker image inspect "$image" >/dev/null

docker run --rm --entrypoint /usr/bin/bash "$image" -lc '
set -euo pipefail

pacman -Q \
  umu-launcher gamescope \
  mangohud \
  gamemode lib32-gamemode

pacman -T
id -nG omarchy | grep -qw gamemode
test ! -e /etc/security/limits.d/10-gamemode.conf
flatpak info com.valvesoftware.Steam >/dev/null

for command in umu-run gamescope mangohud gamemoderun; do
  command -v "$command" >/dev/null || {
    echo "missing gaming command: $command" >&2
    exit 1
  }
done

if pacman -Qq | grep -Eq "^(steam|nvidia-utils|lib32-nvidia.*|lib32-libglvnd|lib32-mesa|lib32-mangohud|vulkan-asahi)$"; then
  echo "gaming image contains an unexpected native Steam/graphics package" >&2
  exit 1
fi

echo "gaming image smoke checks passed"
'
