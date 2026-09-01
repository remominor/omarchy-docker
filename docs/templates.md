# Deployment templates

The development branch provides three runtime profiles. They share one
Dockerfile and the same pinned CUDA-enabled Sunshine package; the Compose file
selects the input topology.

| File | Use when | Host input requirement | Special mapping |
|---|---|---|---|
| `compose.headless.yml` | Virtual input is sufficient | None | `/run/udev` read-only; published Sunshine ports |
| `compose.bridge.yml` | Physical keyboard/mouse must be grabbed exclusively | None | `/dev/input` read-only at `/host/input` |
| `compose.seat9.yml` | Existing seat9 isolation is preferred | Install `host/72-omarchy-sunshine-seat.rules` | No host `/dev/input` mapping |

All profiles persist `/home/omarchy` and `/config` through host bind mounts.
Compose defaults to the repository-relative `./appdata/omarchy-docker` path;
set `APPDATA_PATH` in `.env` for another local path.
The Unraid XML intentionally keeps its `/mnt/user/appdata/...` defaults.

The default capability set retains `MKNOD` because the seat9 profile creates
private event nodes with `omarchy-sync-input-nodes`. The bridge and headless
profiles do not use `MKNOD`; it can be removed in a deployment that never uses
seat9, but is left enabled by default so the shared image remains selectable.
Only the bridge profile reads host event nodes. Seat9 and headless run the
virtual event-node discovery helper; in headless mode it only exposes
Sunshine-created devices and never reads or grabs host input.

Start one profile at a time:

```bash
docker compose --env-file .env -f compose.headless.yml up -d --build
docker compose --env-file .env -f compose.bridge.yml up -d --build
docker compose --env-file .env -f compose.seat9.yml up -d --build
```

## Sunshine networking

All Compose templates publish Sunshine's standard ports. If the host already
runs Sunshine, create a custom Docker network with its own LAN-reachable IP
(for example a macvlan/ipvlan or Unraid custom network) and attach the
container to it; then the ports are available directly on that IP without
host-port conflicts. A user-defined bridge network still requires the listed
port mappings. Changing Sunshine to arbitrary host ports is not a reliable
workaround because some Moonlight clients expect the standard port set.

## Useful Omarchy checks

For broader Omarchy smoke testing, rebuild with `OMARCHY_PROFILE=full`, then
exercise the desktop from Moonlight: Quickshell panels, terminal, Chromium,
Nautilus, clipboard, screenshots, audio, suspend/reconnect behavior, and
Sunshine's NVENC stream. Use `omarchy-container-check` before and after each
change so rendering, user services, audio, and input failures stay distinct.

## Follow-up feature checklist

Use this checklist after monthly image rebuilds or when changing the desktop
profile. It is intentionally manual and is not a bridge acceptance gate.

- [x] Start the bridge profile and confirm its health status becomes `healthy`.
- [x] Confirm the named Hyprland output has the requested resolution, refresh,
  and scale.
- [x] Open Quickshell panels, Foot, Chromium, and Nautilus through Moonlight.
- [x] Verify clipboard copy/paste, screenshots, notifications, and file dialogs.
- [x] Verify PipeWire audio reaches Sunshine and survives a reconnect.
- [x] Confirm Sunshine reports NVENC rather than software encoding.
- [x] If SSH is enabled, recreate the container and confirm the authorized key
  still works and `sshd` starts automatically; containers without an
  authorized key must remain SSH-disabled.
- [x] Reconnect Moonlight repeatedly and check for stale sessions or stuck
  keyboard/pointer buttons.
- [x] Confirm startup logs report `/dev/uinput is available`; if it is missing,
  Sunshine cannot create virtual keyboard, mouse, or gamepad devices. With a
  controller connected, confirm its Sunshine gamepad event node also appears
  under `/dev/input` and is visible to Steam.
- [x] For `bridge`, run `omarchy-container-check` and confirm `/host/input` is
  read-only and bridge readiness is published.
- [ ] For `seat9`, verify the host rule is installed and host input remains
  isolated.
- [ ] Exercise additional Omarchy applications included by the `full` profile
  before promoting the rebuilt image.

## Persistence and updates

The home bind mount is the durable location for user configuration and
per-user application data. Omarchy's preferred software path remains its
native Arch/Omarchy packages (and AUR where appropriate); package-manager
changes made with `pacman` modify the writable container layer and are lost on
recreation, so bake those changes into a rebuild. Use a per-user installer
under `/home/omarchy` when an application supports it.

### Future user bootstrap hook

If recurring setup becomes necessary, a future enhancement could run scripts
from the persistent, user-owned directory
`/home/omarchy/.config/omarchy-container/bootstrap.d/` during session startup.
That hook should be limited to user-level configuration and installs, such as
`~/.local` tools. Flatpak is available as an optional fallback for applications
not available through Omarchy's native package path, but is not required by
Omarchy. The hook should not become a root package
install mechanism: system packages belong in the Dockerfile/profile and the
monthly image rebuild process.

Flatpak is available in the image, but the general image does not preinstall a
Steam implementation. Omarchy's Install menu exposes two distinct choices:

- **Steam (native)** retains Omarchy's original Pacman installer. The package
  is container-layer state and must be reinstalled after image recreation.
- **Steam (Flatpak, persistent)** installs the application and its data under
  `/home/omarchy`, so both survive recreation. This is the recommended NVIDIA
  path until the host runtime supplies matching 32-bit driver libraries.

Other Flatpak applications should also be installed as the `omarchy` user so
they persist in the home bind mount:

```bash
docker exec -it omarchy bash -lc \
  'runuser -u omarchy -- flatpak --user install flathub APP_ID'
```

Do not install user applications with root `flatpak install`, and do not add a
`/proc` bind mount:
Docker already provides proc, and Flatpak setup must tolerate an existing proc
mount (`mountpoint -q /proc || mount -t proc none /proc`). The `/dev/fuse`,
`SYS_ADMIN`, and unconfined seccomp settings already present in the non-headless
profiles are the relevant prerequisites.

### Why native Steam is not the default

Native Steam requires working 32-bit OpenGL and Vulkan libraries. NVIDIA
Container Toolkit supplies the active host driver's 64-bit userspace to the
container, so the image deliberately excludes `nvidia-utils` from Pacman
transactions. Pacman therefore cannot reliably infer which
`lib32-nvidia-utils` provider matches that injected driver.

This failure was reproduced on a host running NVIDIA `610.57.04`: installing
the native `steam` package automatically selected
`lib32-nvidia-580xx-utils 580.178.04`. `glxinfo32 -B` then failed with
`X_GLXCreateNewContext`/`BadValue`, and Steam crashed immediately after its
updater started. Replacing that package in a disposable test container with
the matching `lib32-nvidia-utils 610.57.04` made `glxinfo32` pass and native
Steam successfully opened its sign-in window. This confirms a driver-library
version mismatch rather than a fundamental Xwayland or Hyprland limitation.

The missing host-matched 32-bit stack is a confirmed NVIDIA Container Toolkit
CDI discovery defect, not an Arch `/usr/lib32` layout problem. Toolkit `1.18.0`
changed `auto` mode from the legacy injection path to just-in-time CDI. The
released CDI locator reads the host linker cache but discards its 32-bit result,
even when `NVIDIA_DRIVER_CAPABILITIES=all` requests `compat32`. The legacy
`nvidia-container-cli --compat32` path still discovers the same host files.

NVIDIA's open
[`nvidia-container-toolkit` PR #2035](https://github.com/NVIDIA/nvidia-container-toolkit/pull/2035)
adds the omitted 32-bit linker-cache entries to CDI discovery and explicitly
targets Steam, Proton, 32-bit Vulkan, CUDA, and VDPAU consumers. It supersedes
[PR #1968](https://github.com/NVIDIA/nvidia-container-toolkit/pull/1968) and
has not yet shipped in a toolkit release. NVIDIA's
[runtime documentation](https://github.com/NVIDIA/nvidia-container-toolkit/blob/main/cmd/nvidia-container-runtime/README.md)
defines `compat32` as required for 32-bit applications, while the
[1.18 release notes](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/release-notes.html)
record the switch to JIT-CDI as the default runtime mode.

The proposed fix was also compiled locally from PR #2035 and used to generate
a temporary CDI specification against an Arch host with driver `610.57.04`.
The released `1.20.0` path produced no compat32 mounts; the patched generator
produced 22 `/usr/lib32` mounts, including matching `libGLX_nvidia`, `libcuda`,
`libnvidia-glcore`, and `libnvidia-tls`. This verifies the proposed discovery
fix without replacing the host's installed container runtime.

Pacman can also choose an unrelated generic Vulkan provider when
`nvidia-utils` is excluded, because it does not know that NVIDIA Container
Toolkit has already supplied the 64-bit implementation. Forcing packages with
dependency checks disabled was useful to prove the diagnosis, but is not a
supported deployment solution.

For troubleshooting, compare the active driver and installed 32-bit provider,
then test the 32-bit GLX path:

```bash
nvidia-smi --query-gpu=driver_version --format=csv,noheader
pacman -Q | grep -E 'lib32-(nvidia|libglvnd|mesa)'
glxinfo32 -B
```

A native driver package baked into a generic image would only work for hosts
using that exact NVIDIA driver branch. Dynamically downloading a matching
package is also fragile because the version may no longer be in the current
repositories and would mutate the container system layer. The menu's
persistent Flatpak Steam option remains the supported portable path. Native
Steam is an advanced, host-driver-specific experiment until NVIDIA ships the
CDI fix broadly.

### Steam comparison images

Two thin diagnostic Dockerfiles allow the same Omarchy base image to be tested
with either Steam packaging path without duplicating the main image build:

- `dockerfile.flatpak-steam` keeps Steam from Flathub and its matching NVIDIA
  GL/GL32 runtime extensions.
- `dockerfile.native-steam` installs Arch Steam plus the repository's current
  `lib32-nvidia-utils`, while leaving the 64-bit driver to NVIDIA Container
  Toolkit. A host/repository version mismatch is intentionally visible in this
  image rather than hidden.

Build the common base and both variants:

```bash
docker build -t local/omarchy-base:dev .
docker build -f dockerfile.flatpak-steam \
  --build-arg BASE_IMAGE=local/omarchy-base:dev \
  -t local/omarchy-flatpak-steam:test .
docker build -f dockerfile.native-steam \
  --build-arg BASE_IMAGE=local/omarchy-base:dev \
  -t local/omarchy-native-steam:test .
```

Every Compose template accepts `OMARCHY_IMAGE` for selecting a prebuilt test
image. Do not pass `--build` for this step, because that would rebuild the main
Dockerfile over the selected tag:

```bash
OMARCHY_IMAGE=local/omarchy-flatpak-steam:test \
  docker compose --env-file .env -f compose.bridge.yml \
  up -d --no-build --force-recreate

# Switch the same persistent desktop to the native comparison image.
OMARCHY_IMAGE=local/omarchy-native-steam:test \
  docker compose --env-file .env -f compose.bridge.yml \
  up -d --no-build --force-recreate
```

For the native image, record all three values before testing Steam:

```bash
nvidia-smi --query-gpu=driver_version --format=csv,noheader
pacman -Q lib32-nvidia-utils
glxinfo32 -B
```

### Gaming image variant

`dockerfile.gaming` is the native-package gaming variant. It layers Steam, UMU
Launcher, Gamescope, MangoHud (64/32-bit), and GameMode (64/32-bit) over the
normal full Omarchy image.

It installs a deliberately empty Pacman provider for `vulkan-driver` and
`lib32-vulkan-driver`. This records that the vendor implementation is owned by
the container runtime and avoids baking a host-specific NVIDIA driver into the
image. It does not manufacture missing libraries: on NVIDIA, the host must use
a Container Toolkit release containing the JIT-CDI compat32 fix (or an
equivalent corrected CDI specification) before native Steam/UMU can be
considered supported.

The variant removes GameMode's host-oriented PAM `nice -10` limit. Docker's
fixed inherited hard limit makes that rule fail during `systemd --user` startup;
removing it keeps the desktop bootable while preserving the GameMode daemon and
the container's `SYS_NICE` capability.

Build and select it with:

```bash
docker build -t local/omarchy-base:dev .
docker build -f dockerfile.gaming \
  --build-arg BASE_IMAGE=local/omarchy-base:dev \
  -t local/omarchy-gaming:test .
./tests/smoke-gaming-image.sh local/omarchy-gaming:test

OMARCHY_IMAGE=local/omarchy-gaming:test \
  docker compose --env-file .env -f compose.bridge.yml \
  up -d --no-build --force-recreate
```

The Flatpak Steam menu choice remains present in the gaming image as a
portable fallback. Native packages are baked and reproducible; game libraries,
Steam state, UMU prefixes, and user configuration remain under the persistent
home mapping.

Monthly Omarchy releases should produce a new image from `main`; live CVE
patches can be installed for testing, then incorporated into the next image
rebuild. The `main` workflow publishes the headless image as both `latest` and
`headless` to GHCR.
