FROM archlinux:base-devel

ARG OMARCHY_CHANNEL=stable
ARG OMARCHY_PROFILE=core
ARG FAKE_UDEV_COMMIT=903a070b9eee733e146dfe03dc18173caf9eb010
ARG FAKE_UDEV_SHA256=69368c27a7b996e3e086f1a96d8a9f3ef19c2fdaf5b5ace9ea6b7f34b45f4681
ARG SUNSHINE_VERSION=2026.516.143833
ARG SUNSHINE_SHA256=676539cfb079f81f38e7001b85d72c39cd5bf25abe0956bab4577cb25afc5299

ENV container=docker \
    OMARCHY_PATH=/usr/share/omarchy

# Generate the locale before exporting it so package hooks do not emit locale
# warnings throughout the build.
RUN echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen && locale-gen

ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    XDG_DATA_DIRS=/home/omarchy/.local/share/flatpak/exports/share:/var/lib/flatpak/exports/share:/usr/local/share:/usr/share

# Use Omarchy's Arch snapshot and Omarchy package repo as one coherent package set.
RUN set -eux; \
    case "${OMARCHY_CHANNEL}" in \
      stable) ARCH_MIRROR='https://stable-mirror.omarchy.org/$repo/os/$arch'; OMARCHY_REPO='https://pkgs.omarchy.org/stable/$arch' ;; \
      edge)   ARCH_MIRROR='https://mirror.omarchy.org/$repo/os/$arch'; OMARCHY_REPO='https://pkgs.omarchy.org/edge/$arch' ;; \
      *) echo "Unsupported OMARCHY_CHANNEL=${OMARCHY_CHANNEL}" >&2; exit 2 ;; \
    esac; \
    printf 'Server = %s\n' "${ARCH_MIRROR}" > /etc/pacman.d/mirrorlist; \
    printf '\n[omarchy]\nSigLevel = Optional TrustAll\nServer = %s\n' "${OMARCHY_REPO}" >> /etc/pacman.conf; \
    pacman -Syy --noconfirm; \
    pacman -S --noconfirm --needed archlinux-keyring; \
    pacman-key --init; \
    pacman-key --populate archlinux; \
    install -d /tmp/container-install-shims; \
    printf '#!/bin/sh\nexit 0\n' > /tmp/container-install-shims/modprobe; \
    printf '#!/bin/sh\nexit 0\n' > /tmp/container-install-shims/udevadm; \
    chmod 0755 /tmp/container-install-shims/modprobe /tmp/container-install-shims/udevadm; \
    PATH="/tmp/container-install-shims:${PATH}" pacman -Syu --noconfirm --needed \
      bash bash-completion ca-certificates curl dbus git glib2 \
      sudo shadow util-linux procps-ng iproute2 iputils jq perl \
      systemd polkit pambase libinput evtest flatpak xdg-desktop-portal \
      base-devel fakeroot pacman-contrib \
      libglvnd egl-wayland vulkan-icd-loader libva mesa-utils vulkan-tools; \
    rm -rf /tmp/container-install-shims

# Build the optional bridge binary once; the compose template selects whether
# it is used at runtime.
RUN set -eux; \
    pacman -S --noconfirm --needed wayland libxkbcommon

# Install the statically linked Games-on-Whales fake-udev emitter from a
# pinned revision. It publishes synthetic GROUP_UDEV events inside the
# container network namespace after private input nodes are materialized.
RUN set -eux; \
    curl -fsSL \
      "https://raw.githubusercontent.com/XT-Martinez/labwc-headless-docker/${FAKE_UDEV_COMMIT}/executables/fake-udev" \
      -o /usr/local/bin/fake-udev; \
    printf '%s  %s\n' "${FAKE_UDEV_SHA256}" /usr/local/bin/fake-udev | sha256sum -c -; \
    chmod 0755 /usr/local/bin/fake-udev

# Omarchy's meta package intentionally hard-depends on boot-machine components.
# Supply empty "provides" packages for the pieces a Docker desktop must not own,
# then install Omarchy normally so all *other* runtime dependencies stay current.
RUN set -eux; \
    useradd -m -s /bin/bash packagebuilder; \
    mkdir -p /tmp/omarchy-container-stubs; \
    chown packagebuilder:packagebuilder /tmp/omarchy-container-stubs; \
    printf '%s\n' \
      'pkgname=omarchy-container-stubs' \
      'pkgver=1' \
      'pkgrel=1' \
      'pkgdesc="Container-only virtual providers for Omarchy machine dependencies"' \
      'arch=("any")' \
      'provides=("limine" "limine-mkinitcpio-hook" "limine-snapper-sync" "snapper" "sddm")' \
      'package() { mkdir -p "$pkgdir/usr/share/omarchy-container"; printf "container stub\n" > "$pkgdir/usr/share/omarchy-container/stubs"; }' \
      > /tmp/omarchy-container-stubs/PKGBUILD; \
    runuser -u packagebuilder -- bash -lc 'cd /tmp/omarchy-container-stubs && makepkg --noconfirm'; \
    pacman -U --noconfirm /tmp/omarchy-container-stubs/omarchy-container-stubs-1-1-any.pkg.tar.*; \
    pacman -S --noconfirm --needed omarchy-settings omarchy; \
    sed -i 's/^SigLevel = Optional TrustAll$/SigLevel = Required DatabaseOptional/' /etc/pacman.conf; \
    pacman -Syy --noconfirm; \
    case "${OMARCHY_PROFILE}" in \
      core) \
        pacman -S --noconfirm --needed \
          alsa-utils pipewire-alsa pipewire-jack pipewire-pulse \
          xdg-desktop-portal-gtk xdg-terminal-exec \
          xdg-desktop-portal flatpak \
          foot neovim nano less chromium nautilus udiskie \
          wl-clipboard grim slurp socat pamixer \
          hyprpicker hyprsunset gpu-screen-recorder \
          fastfetch imv inotify-tools \
          noto-fonts noto-fonts-cjk noto-fonts-emoji ;; \
      full) \
        grep -Ev '^(#|[[:space:]]*$|asdcontrol|bolt|brightnessctl|ddcutil|docker|docker-buildx|docker-compose|kernel-modules-hook|networkmanager|power-profiles-daemon|qemu-user-static-binfmt|sddm|ufw|ufw-docker)$' \
          /usr/share/omarchy/install/omarchy-base.packages > /tmp/omarchy-container-full.packages; \
        pacman -S --noconfirm --needed $(cat /tmp/omarchy-container-full.packages); \
        rm -f /tmp/omarchy-container-full.packages; \
        pacman -S --noconfirm --needed pipewire-alsa pipewire-jack pipewire-pulse ;; \
      *) echo "Unsupported OMARCHY_PROFILE=${OMARCHY_PROFILE}; use core or full" >&2; exit 2 ;; \
    esac; \
    flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo; \
    userdel -r packagebuilder; \
    rm -rf /tmp/omarchy-container-stubs; \
    pacman -Scc --noconfirm

# Omarchy's current Sunshine build omits the CUDA wlroots-to-NVENC path.
# Install LizardByte's matching Arch release instead, with an immutable URL
# and verified digest, so NVIDIA containers retain hardware encoding.
RUN set -eux; \
    sunshine_package="sunshine-${SUNSHINE_VERSION}-1-x86_64.pkg.tar.zst"; \
    curl -fsSL \
      "https://github.com/LizardByte/Sunshine/releases/download/v${SUNSHINE_VERSION}/${sunshine_package}" \
      -o "/tmp/${sunshine_package}"; \
    printf '%s  %s\n' "${SUNSHINE_SHA256}" "/tmp/${sunshine_package}" | sha256sum -c -; \
    pacman -U --noconfirm "/tmp/${sunshine_package}"; \
    rm -f "/tmp/${sunshine_package}"

RUN set -eux; \
    useradd -m -u 1000 -G wheel,audio,video,input,seat -s /bin/bash omarchy; \
    cp -aT /etc/skel /home/omarchy; \
    chown -R 1000:1000 /home/omarchy; \
    mkdir -p /opt/omarchy-home-seed /var/lib/systemd/linger /config; \
    cp -aT /home/omarchy /opt/omarchy-home-seed; \
    touch /var/lib/systemd/linger/omarchy; \
    chown 1000:1000 /config; \
    systemctl mask \
      getty@.service console-getty.service \
      systemd-udevd.service \
      systemd-udevd-control.socket \
      systemd-udevd-kernel.socket \
      systemd-remount-fs.service \
      systemd-random-seed.service \
      systemd-machine-id-commit.service \
      || true

COPY rootfs/ /
COPY src/input-bridge/ /tmp/omarchy-input-bridge/

RUN set -eux; \
    make -C /tmp/omarchy-input-bridge install; \
    rm -rf /tmp/omarchy-input-bridge

RUN set -eux; \
    chmod 0755 \
      /usr/local/bin/omarchy-container-session \
      /usr/local/bin/omarchy-headless-init \
      /usr/local/bin/omarchy-monitor-guard \
      /usr/local/bin/omarchy-sunshine \
      /usr/local/bin/omarchy-sync-input-nodes \
      /usr/local/bin/omarchy-container-check \
      /usr/local/bin/omarchy-audio-init \
      /usr/local/sbin/omarchy-container-init \
      /usr/local/sbin/omarchy-input-bridge-service \
      /usr/local/sbin/omarchy-input-bridge-failsafe \
      /usr/local/sbin/omarchy-start-user; \
    chmod 0755 /usr/share/omarchy/bin/omarchy-refresh-hyprland; \
    install -Dm0755 /usr/share/omarchy/bin/omarchy-refresh-hyprland /usr/bin/omarchy-refresh-hyprland; \
    chmod 0750 /etc/sudoers.d; \
    chmod 0440 /etc/sudoers.d/omarchy-container; \
    systemctl enable omarchy-user.service seatd.service; \
    chown -R 1000:1000 /opt/omarchy-home-seed

STOPSIGNAL SIGRTMIN+3
ENTRYPOINT ["/usr/local/sbin/omarchy-container-init"]
CMD ["/usr/lib/systemd/systemd"]
