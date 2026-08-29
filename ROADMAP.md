# Omarchy Docker roadmap

This project is an Arch-based, headless Omarchy desktop for Unraid. Its first
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
- When privileges are tightened, map the actual numeric GIDs of `/dev/dri`,
  `/dev/uinput`, and `/dev/input/event*`; host and container group names do
  not guarantee matching IDs.
- If Sunshine creates an input device in sysfs without a matching node in the
  container's private `/dev`, materialize the event node from the sysfs
  major/minor instead of making all devices world-writable.
- Keep PipeWire, WirePlumber, and a predictable null sink as the default
  headless audio path.
- Treat Sunshine Web UI CSRF origins and client-driven output resolution as
  explicit future configuration, not hidden startup behavior.

## Delivery phases

### 1. Reproducible image

- Build the `core` profile from a clean cache.
- Confirm the Omarchy stable repository and Arch snapshot remain coherent.
- Add an image-level smoke test for required commands, service units, and
  seeded configuration.

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

- Verify Moonlight keyboard, mouse, and controller input.
- Correlate `/sys/class/input`, `/dev/input/event*`, and `/dev/uinput` if input
  is incomplete.
- Only after the dedicated-GPU path passes, test coexistence and then optional
  same-GPU sharing with `steam-wayland`.

### 5. Hardening

- Replace `privileged: true` with explicit devices, capabilities, and device
  cgroup rules.
- Add runtime numeric-GID mapping before dropping broad device access.
- Keep host networking only where it is needed; compare it with the dedicated
  IP configuration.
- Remove passwordless elevation that is no longer required.

### 6. Quality-of-life features

- Apply `SUNSHINE_CLIENT_WIDTH`, `SUNSHINE_CLIENT_HEIGHT`, and
  `SUNSHINE_CLIENT_FPS` to the named Hyprland output with a known-good fallback.
- Add an environment-controlled Sunshine CSRF origin list without overwriting
  user-managed configuration.
- Add an Unraid Community Applications template after the runtime contract is
  stable.

## First-pass acceptance checklist

- [ ] Clean `core` image build succeeds.
- [ ] Container reaches a healthy systemd state.
- [ ] NVIDIA EGL and Vulkan use the selected GPU rather than llvmpipe.
- [ ] Named Hyprland headless output is active at scale 1.
- [ ] Omarchy desktop is visible before a streaming client connects.
- [ ] Sunshine captures through Wayland and initializes NVENC.
- [ ] Moonlight video, audio, keyboard, mouse, and controller work.
- [ ] Persistent home and Sunshine credentials survive recreation.
- [ ] Dedicated-IP and shifted-port host-network modes are documented and
      tested separately.
- [ ] Same-GPU coexistence is tested only after the independent path passes.
