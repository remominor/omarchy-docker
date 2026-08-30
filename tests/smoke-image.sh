#!/usr/bin/env bash
set -euo pipefail

image="${1:-local/omarchy-unraid:test}"

docker image inspect "$image" >/dev/null

docker run --rm --entrypoint /usr/bin/bash "$image" -lc '
set -euo pipefail

for command in \
  Hyprland hyprctl quickshell uwsm sunshine \
  pipewire wireplumber pactl chromium foot nautilus jq systemctl \
  evtest fake-udev flatpak; do
  command -v "$command" >/dev/null || {
    echo "missing command: $command" >&2
    exit 1
  }
done

for path in \
  /usr/local/bin/omarchy-container-session \
  /usr/local/bin/omarchy-headless-init \
  /usr/local/bin/omarchy-sunshine \
  /usr/local/bin/omarchy-sync-input-nodes \
  /usr/local/bin/omarchy-container-check \
  /usr/local/bin/omarchy-audio-init \
  /usr/local/sbin/omarchy-container-init \
  /usr/local/sbin/omarchy-start-user \
  /etc/systemd/system/omarchy-user.service \
  /etc/systemd/user/omarchy-desktop.service \
  /etc/systemd/user/omarchy-headless-init.service \
  /etc/systemd/user/omarchy-sunshine.service \
  /opt/omarchy-home-seed/.config/hypr/autostart.lua; do
  test -e "$path" || {
    echo "missing path: $path" >&2
    exit 1
  }
done

for script in /usr/local/bin/omarchy-* /usr/local/sbin/omarchy-*; do
  bash -n "$script"
done

systemctl is-enabled omarchy-user.service | grep -qx enabled
systemctl is-enabled seatd.service | grep -qx enabled
id -nG omarchy | grep -qw seat
test "$(systemctl is-enabled systemd-udevd.service || true)" = masked
test "$(systemctl is-enabled systemd-udevd-control.socket || true)" = masked
test "$(systemctl is-enabled systemd-udevd-kernel.socket || true)" = masked
locale -a | grep -qi "^en_US.utf8$"
test "$(stat -c %a /etc/sudoers.d)" = 750
visudo -cf /etc/sudoers.d/omarchy-container
! ldd /usr/bin/Hyprland | grep -q "not found"
! ldd /usr/bin/sunshine | grep -q "not found"
grep -A2 -F "[omarchy]" /etc/pacman.conf | \
  grep -Fqx "SigLevel = Required DatabaseOptional"

pacman -Q \
  omarchy omarchy-settings sunshine hyprland quickshell uwsm flatpak \
  pipewire wireplumber

echo "image smoke checks passed"
'
