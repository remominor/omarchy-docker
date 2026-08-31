# Omarchy in Docker

This project runs **Omarchy 4's userspace desktop** in Docker rather than placing
QEMU inside Docker. It installs the Omarchy runtime/settings payloads, Hyprland,
Quickshell and UWSM, creates a named Hyprland headless output, then streams that
output with Sunshine using NVIDIA NVENC. It supports general Linux Docker hosts;
Unraid is one documented deployment target.

It deliberately **does not install Omarchy's machine-level boot stack**
(Limine, Snapper, SDDM). The image installs tiny virtual-provider stubs for
those machine-only dependencies and then installs the normal `omarchy` and
`omarchy-settings` packages. That lets pacman pull Omarchy's complete current
runtime dependency set without giving the container a bootloader, snapshot
manager, or display manager.


## Image profile

The default is:

```ini
OMARCHY_PROFILE=full
```

`full` installs the real Omarchy 4 runtime/settings packages plus nearly all
packages from Omarchy's current base manifest, with machine-only components
filtered for Docker. This includes Omarchy's `cliamp`, `mise`, and `yay`
tooling, along with the desktop, development, and application utilities. The
Arch `multilib` repository is enabled so Steam and 32-bit graphics dependencies
can resolve.

`core` installs a smaller curated runtime set for minimal images. It filters
that make no sense or are unsafe in a Docker desktop (boot/session manager,
host firewall/network manager, kernel hooks, nested Docker, power/display
hardware controls, and binfmt registration).

Use `core` when image size and a smaller package surface are more important than
matching the complete Omarchy userspace.

## Local build validation

Build and load the default `core` image with Buildx:

```bash
docker buildx build --load --progress=plain \
  --build-arg OMARCHY_CHANNEL=stable \
  --build-arg OMARCHY_PROFILE=full \
  -t local/omarchy:test .
```

Then validate its packages, commands, service files, permissions, linked
libraries, and seeded configuration:

```bash
./tests/smoke-image.sh local/omarchy:test
```

This smoke test does not prove GPU rendering, Wayland capture, NVENC, or input.
Those require starting the container with NVIDIA, DRM, and uinput devices.

## Deployment templates

The project has three selectable Compose templates:

| Template | Input path | Host requirement |
|---|---|---|
| `compose.headless.yml` | Sunshine virtual input only | No host input devices or seat rule |
| `compose.bridge.yml` | Custom Wayland bridge with exclusive evdev grabs | Read-only `/dev/input` mount |
| `compose.seat9.yml` | Sunshine evdev nodes mirrored into private `/dev` | Host `seat9` udev rule |

All templates use the same pinned CUDA-enabled Sunshine package. Choose one
template at a time; do not run multiple templates with the default container
name. Detailed input and persistence notes are in
[Deployment templates](docs/templates.md).

For a local Docker host, create the environment file and select a GPU:

```bash
cp .env.example .env
nvidia-smi --query-gpu=index,name,uuid,pci.bus_id --format=csv
nano .env
```

Put the UUID of the GPU you want into:

```ini
NVIDIA_VISIBLE_DEVICES=GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

```bash
docker compose --env-file .env -f compose.bridge.yml up -d --build
docker logs -f omarchy
```

The bridge and seat9 profiles publish Sunshine's standard ports. If host
Sunshine must remain active, use a custom Docker network with a dedicated,
LAN-reachable container IP and keep those standard ports on the dedicated IP.
Do not rely on arbitrary Sunshine port remapping: some Moonlight clients do
not connect to non-default ports. The Omarchy setup does not alter the host
Sunshine service or environment.

Install the host seat rule only when using the `seat9` template, as described
in [Input isolation](docs/input-isolation.md).

## Unraid deployment

Unraid deployment is defined by
[unraid/omarchy-docker.xml](unraid/omarchy-docker.xml), not Docker Compose.
Build the image on Unraid before adding the template:

```bash
docker build \
  --build-arg OMARCHY_CHANNEL=stable \
  --build-arg OMARCHY_PROFILE=full \
  -t local/omarchy-headless:latest .
```

Copy the XML into Unraid's user-template directory if it is not being installed
through a template repository:

```bash
cp unraid/omarchy-docker.xml \
  /boot/config/plugins/dockerMan/templates-user/my-omarchy-docker.xml
```

The XML follows the simple headless profile: it uses host networking and
privileged mode, so no input rule or device-by-device mapping is required.
Stop any other Sunshine instance using the host's standard ports before
starting it. If host Sunshine must remain active, use one of the non-headless
Compose profiles on a custom network with a dedicated IP instead.

The template's **Overview**, **Requires**, and individual field descriptions
document the complete runtime contract. In particular:

- use the NVIDIA runtime, cgroup mount, and tmpfs mounts from `ExtraParams`;
- persist only the Omarchy home and Sunshine configuration paths;
- keep standard Sunshine ports because Moonlight clients may not support
  arbitrary remapped ports.

Sunshine uses its normal base port 47989 on the host network. Its web UI is:

```text
https://<DEDICATED-IP>:47990
```

Create the Sunshine web UI account, pair Moonlight, then launch **Omarchy
Desktop**.

The headless profile is intended for a host where those ports are available.
For host-desktop input isolation without host changes, use the bridge profile;
for the seat-based fallback, use the seat9 profile and its input guide.

If Sunshine is accessed through a non-default hostname or address, add its
complete browser origin to `.env` on a local host or the XML variable on Unraid:

```ini
SUNSHINE_CSRF_ALLOWED_ORIGINS=https://192.168.1.250:47990
```

Multiple origins may be comma-separated. Include the protocol and port, and
only list origins you trust.

Sunshine custom ports are deliberately not used because they do not work with
every Moonlight client.


## Headless audio

The container explicitly starts PipeWire, PipeWire Pulse and WirePlumber, then
creates a null sink named `omarchy_stream` and makes its monitor the default
capture source. This gives Sunshine a stable desktop-audio target even when
the Unraid server has no physical audio output.

Check it with:

```bash
docker exec -it omarchy omarchy-container-check
```

Look for `omarchy_stream` under the **AUDIO** section.

## Virtual display settings

Runtime defaults (`.env` for local Compose and XML fields on Unraid):

```ini
OMARCHY_OUTPUT_NAME=OMARCHY
OMARCHY_RESOLUTION=2560x1440
OMARCHY_REFRESH=60
OMARCHY_SCALE=1
```

Hyprland supports named headless outputs and explicitly documents them for
VNC/RDP/Sunshine use.

These values describe the compositor output captured by Sunshine; Moonlight's
client resolution is not negotiated back to Hyprland. For a 1080p client, use
`OMARCHY_RESOLUTION=1920x1080` (or keep 2560x1440 and adjust `OMARCHY_SCALE`).
Apply changes by recreating the container, for example:

```bash
OMARCHY_RESOLUTION=1920x1080 docker compose --env-file .env -f compose.bridge.yml up -d
```

The Omarchy display utility can also write a monitor rule to
`~/.config/hypr/monitors.lua`; that user rule may change scale after startup.
The display utility may continue listing disabled HDMI/DP connectors because
the GPU DRM device remains available for rendering. They are disabled outputs,
not additional active Sunshine displays.

The container adds a small final Hyprland module to the user config. It keeps
unspecified outputs disabled and explicitly configures `OMARCHY`, so display
settings reloads do not let physical connectors claim workspaces. This rule
does not disable or replace the GPU/DRM rendering path.

## GPU / DRM selection

The templates retain the full `/dev/dri` mapping for NVIDIA compatibility. The
render-node pattern used by the Labwc reference containers does not mean
render-only access: those containers map both card and render nodes and set
wlroots' `WLR_RENDER_DRM_DEVICE`. Hyprland uses Aquamarine instead, where
`AQ_DRM_DEVICES` selects the DRM GPU and should not be pointed at a render node
alone. The monitor guard remains the protection against physical outputs.

A useful host-side mapping command is:

```bash
for c in /sys/class/drm/card[0-9]*; do
  printf '%s -> ' "$c"
  readlink -f "$c/device"
done
```

Compare the PCI address with `nvidia-smi --query-gpu=pci.bus_id`.

## Diagnostics

The image includes one command intended for the first round of debugging:

```bash
docker exec -it omarchy omarchy-container-check
```

It reports:

- NVIDIA devices visible in the container
- EGL and Vulkan renderer information
- the user systemd manager
- the Omarchy/Hyprland service
- current Hyprland outputs
- Sunshine service state and listening ports
- the headless-output initialization log

Also useful:

```bash
docker exec -it omarchy journalctl -b --no-pager
docker exec -it omarchy journalctl --user -M omarchy@ --no-pager
docker exec -it omarchy bash
```

From a shell inside the container:

```bash
runuser -u omarchy -- env \
  XDG_RUNTIME_DIR=/run/user/1000 \
  DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
  hyprctl monitors all
```

## Persistent data

The Unraid XML template persists:

```text
/mnt/user/appdata/omarchy-docker/home      -> /home/omarchy
/mnt/user/appdata/omarchy-docker/sunshine  -> /config
```

The local Compose templates default to `./appdata/omarchy-docker` in the
project directory; override `APPDATA_PATH` when starting on another host.

The image copies Omarchy's `/etc/skel` defaults into an empty persistent home
on first boot. Existing files are not overwritten. Per-user application data
and Flatpak installations belong in that home bind mount; image-level package
changes require a rebuild. See [Deployment templates](docs/templates.md) for
the update and Flatpak/proc guidance.

For a clean retry, stop and remove the container, move the two persistent
directories aside, then recreate it from Compose or DockerMan. Moving them
aside preserves Sunshine pairing and the Omarchy home for recovery.

## Security note

The bridge and seat9 Compose/XML deployments run with `privileged: false`.
They expose only the required device classes and a bounded set of capabilities,
including `SYS_ADMIN`, `MKNOD`, and `NET_ADMIN`. The headless template retains
privileged mode for the simplest virtual-input setup. Every profile is still a
high-trust desktop container, and its persistent user has passwordless
sudo/polkit authorization inside the container.

Host input mappings are intentional: headless maps no input tree, bridge maps
`/dev/input` read-only solely for its exclusive grabs, and seat9 maps neither
host input nor host udev. Do not add broad mappings to the latter two as a
troubleshooting shortcut; that defeats their isolation boundary.

## Expected first failure points

If it does not come up immediately, the most useful distinction is:

1. **Hyprland does not start** — inspect `journalctl` and `AQ_DRM_DEVICES`.
2. **Hyprland starts but renderer is llvmpipe** — NVIDIA EGL/GBM visibility is
   the next thing to fix.
3. **Hyprland + Quickshell work but Sunshine cannot capture** — inspect
   `WAYLAND_DISPLAY`, the `wlr` capture log, and the named `OMARCHY` output.
4. **Sunshine captures but NVENC fails** — NVIDIA `video` capability or driver
   library injection is the problem.
5. **Input reaches both Omarchy and the host** — confirm the `(seat9)` host
   rule is installed, then restart the container so KDE never opens the new
   event nodes as seat0. See [Input isolation](docs/input-isolation.md).
6. **Unraid input works until reboot** — the seat rule was installed only in
   ephemeral `/etc`; persist its installation through `/boot/config/go` as
   described in the XML template and input guide.
7. **Everything works on its own but conflicts with Steam Headless** — then
   test same-GPU coexistence separately; do not change three variables at once.

The project favors transparent configuration and diagnostics so deployments can
be adapted to different Linux hosts and input paths.

## Development roadmap

The staged validation and hardening plan is in [ROADMAP.md](ROADMAP.md). It
also records the container, NVIDIA, Sunshine, input, and audio lessons carried
over from the related `remominor/steam-wayland` work while keeping this image
native to Arch, Omarchy, Hyprland, UWSM, and systemd.
