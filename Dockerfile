FROM archlinux:base-devel

ARG OMARCHY_CHANNEL=stable
ARG OMARCHY_PROFILE=core

ENV container=docker \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    OMARCHY_PATH=/usr/share/omarchy

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
    pacman -Syu --noconfirm --needed \
      bash bash-completion ca-certificates curl dbus git glib2 \
      sudo shadow util-linux procps-ng iproute2 iputils jq perl \
      systemd polkit pambase \
      base-devel fakeroot pacman-contrib \
      libglvnd egl-wayland vulkan-icd-loader libva mesa-utils vulkan-tools \
      sunshine

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
    case "${OMARCHY_PROFILE}" in \
      core) \
        pacman -S --noconfirm --needed \
          alsa-utils pipewire-alsa pipewire-jack pipewire-pulse \
          xdg-desktop-portal-gtk xdg-terminal-exec \
          foot chromium nautilus udiskie \
          wl-clipboard grim slurp socat pamixer \
          hyprpicker hyprsunset gpu-screen-recorder \
          fastfetch imv inotify-tools \
          noto-fonts noto-fonts-cjk noto-fonts-emoji ;; \
      full) \
        grep -Ev '^(asdcontrol|bolt|brightnessctl|ddcutil|docker|docker-buildx|docker-compose|kernel-modules-hook|networkmanager|power-profiles-daemon|qemu-user-static-binfmt|sddm|ufw|ufw-docker)$' \
          /usr/share/omarchy/install/omarchy-base.packages > /tmp/omarchy-container-full.packages; \
        pacman -S --noconfirm --needed $(cat /tmp/omarchy-container-full.packages); \
        rm -f /tmp/omarchy-container-full.packages; \
        pacman -S --noconfirm --needed pipewire-alsa pipewire-jack pipewire-pulse ;; \
      *) echo "Unsupported OMARCHY_PROFILE=${OMARCHY_PROFILE}; use core or full" >&2; exit 2 ;; \
    esac; \
    userdel -r packagebuilder; \
    rm -rf /tmp/omarchy-container-stubs; \
    pacman -Scc --noconfirm

RUN set -eux; \
    echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen; \
    locale-gen; \
    useradd -m -u 1000 -G wheel,audio,video,input -s /bin/bash omarchy; \
    cp -aT /etc/skel /home/omarchy; \
    chown -R 1000:1000 /home/omarchy; \
    mkdir -p /opt/omarchy-home-seed /var/lib/systemd/linger /config; \
    cp -aT /home/omarchy /opt/omarchy-home-seed; \
    touch /var/lib/systemd/linger/omarchy; \
    chown 1000:1000 /config; \
    systemctl mask \
      getty@.service console-getty.service \
      systemd-remount-fs.service \
      systemd-random-seed.service \
      systemd-machine-id-commit.service || true

COPY rootfs/ /

RUN set -eux; \
    chmod 0755 \
      /usr/local/bin/omarchy-container-session \
      /usr/local/bin/omarchy-headless-init \
      /usr/local/bin/omarchy-sunshine \
      /usr/local/bin/omarchy-container-check \
      /usr/local/bin/omarchy-audio-init \
      /usr/local/sbin/omarchy-container-init \
      /usr/local/sbin/omarchy-start-user; \
    chmod 0440 /etc/sudoers.d/omarchy-container; \
    systemctl enable omarchy-user.service; \
    mkdir -p /opt/omarchy-home-seed/.config/hypr; \
    printf '\n-- Docker headless output + Sunshine bootstrap.\no.launch_on_start("/usr/local/bin/omarchy-headless-init")\n' \
      >> /opt/omarchy-home-seed/.config/hypr/autostart.lua; \
    chown -R 1000:1000 /opt/omarchy-home-seed

STOPSIGNAL SIGRTMIN+3
ENTRYPOINT ["/usr/local/sbin/omarchy-container-init"]
CMD ["/usr/lib/systemd/systemd"]
