# Omarchy Docker roadmap

This project is an Arch-based, headless Omarchy desktop. Its first
goal is a reliable Moonlight desktop through Sunshine, not feature parity with
a full Omarchy machine or a general-purpose gaming container.

## Architecture boundary

Omarchy's own stack remains authoritative inside the image:

- Arch Linux and Pacman rather than Debian and APT
- Hyprland/Aquamarine rather than Labwc/wlroots
- UWSM and systemd user services rather than s6
- Omarchy settings and desktop components rather than a custom desktop shell

The related [`remominor/steam-wayland`](https://github.com/remominor/steam-wayland)
project is useful as a record of container, NVIDIA, Sunshine, input, and audio
failure modes. Its Debian-specific package scripts, hard-coded library paths,
and compositor implementation should not be copied into this image.

## Lessons carried forward

- Let NVIDIA Container Toolkit inject the host-matched driver stack. Do not
  install a separate NVIDIA driver in the Arch image.
- Validate GPU rendering and NVENC independently. A working compositor does
  not prove that Sunshine can open an encode session.
- On affected NVIDIA 570/580/595 multi-GPU hosts, test exposing all relevant
  GPUs with the intended primary GPU first if a single exposed GPU produces
  `OpenEncodeSessionEx failed: unsupported device (2)`.
- Prefer a container-owned D-Bus and user systemd manager. Do not bind the
  host system bus into the container.
- The CachyOS Compose file and Unraid XML now run non-privileged with explicit
  devices, capabilities, and input cgroup rules.
- Sunshine input uses a split-seat design: the host assigns uniquely suffixed
  Omarchy devices to seat9, while the private container udev database assigns
  the matching event nodes to seat0 for Hyprland.
- Materializing `/dev/input/event*` is not sufficient for hotplug. Create the
  private udev record and emit a GROUP_UDEV add event after creating the node;
  reverse that sequence on removal.
- Keep PipeWire, WirePlumber, and a predictable null sink as the default
  headless audio path.
- Treat Sunshine Web UI CSRF origins and client-driven output resolution as
  explicit future configuration, not hidden startup behavior.

## Delivery phases

### 1. Reproducible image

- Build the `core` profile from a clean cache (the local bridge rebuild is
  currently validated; CI remains the clean-build gate).
- Confirm the Omarchy stable repository and Arch snapshot remain coherent.
- Add an image-level smoke test for required commands, service units, and
  seeded configuration.
- Keep `core` and `full` profile builds available; use `core` for the default
  headless image and `full` for broader Omarchy software testing.

### 2. Dedicated-GPU compositor proof

- Start with a GPU not used by another streaming container.
- Confirm the NVIDIA runtime, EGL, Vulkan, and the selected DRM node.
- Confirm Hyprland creates the named headless output at the requested mode.
- Confirm Quickshell and the Omarchy desktop appear without a Moonlight or
  browser connection.

### 3. Sunshine video and audio

- Confirm wlroots capture selects the named output.
- Confirm H.264 and HEVC NVENC independently; do not silently accept software
  encoding as success.
- Pair Moonlight and verify desktop audio through `omarchy_stream.monitor`.
- Verify the desktop survives a client disconnect and reconnect.

### 4. Input and coexistence

- Keyboard, pointer motion, and clicks are verified through Moonlight without
  reaching the CachyOS KDE host, including while an RDP session remains
  active. Both Sunshine mouse devices are attached to Hyprland.
- Controller input is verified through Moonlight with a macOS DualSense client;
  Sunshine's virtual gamepad is mirrored into the container for Steam.
- The bridge input path is the default development path; seat9 remains an
  alternative template for hosts that already use the seat rule.
- Only after the dedicated-GPU path passes, test coexistence and then optional
  same-GPU sharing with `steam-wayland`.

### 5. Hardening

- Keep the validated explicit devices, capabilities, and device cgroup rules;
  do not restore privileged mode or broad host input mounts.
- Continue reducing individual capabilities where runtime testing proves they
  are unnecessary.
- Keep the private bridge/custom-network namespace required by fake-udev; do
  not restore host networking.
- Remove passwordless elevation that is no longer required.

### 6. Quality-of-life features

- Apply `SUNSHINE_CLIENT_WIDTH`, `SUNSHINE_CLIENT_HEIGHT`, and
  `SUNSHINE_CLIENT_FPS` to the named Hyprland output with a known-good fallback.
- Add an environment-controlled Sunshine CSRF origin list without overwriting
  user-managed configuration.
- Publish the validated Unraid XML through Community Applications after the
  image distribution path is stable.
- Keep Flatpak enabled with per-user installation under the persistent home
  mapping; rebuild monthly to refresh the base image and security updates.
- Preserve the virtual-output monitor fallback while reading user scaling from
  `monitors.lua`, so physical connector rediscovery cannot steal workspaces.

## First-pass acceptance checklist

- [x] Clean `core` image build succeeds.
- [x] Container reaches a healthy systemd state.
- [x] NVIDIA EGL and Vulkan use the selected GPU rather than llvmpipe.
- [x] Named Hyprland headless output is active at the configured mode.
- [x] Omarchy desktop is visible before a streaming client connects.
- [x] Sunshine captures through Wayland and initializes NVENC.
- [x] Moonlight video, keyboard, and mouse work.
- [x] Moonlight audio works through `omarchy_stream.monitor`.
- [x] Moonlight controller input works, including a macOS DualSense client.
- [x] Flatpak Steam installs and launches games in native Linux and Proton modes.
- [x] Native Steam's GLX crash is diagnosed as a mismatched 32-bit NVIDIA
      userspace; matching it to the injected host driver opens the Steam UI,
      and NVIDIA's open JIT-CDI compat32 fix identifies the upstream cause.
- [x] Flatpak and native Steam comparison Dockerfiles provide reproducible
      packaging and driver-compatibility test images from one Omarchy base.
- [x] Persistent home and Sunshine credentials survive recreation.
- [x] CachyOS bridge Compose and dedicated-IP Unraid XML deployment modes are
      documented separately.
- [x] Same-GPU use between the CachyOS host and Omarchy container is verified;
      simultaneous Moonlight sessions remain untested.
