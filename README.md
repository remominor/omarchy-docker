# Omarchy on Unraid — headless Docker prototype

This image runs **Omarchy 4's userspace desktop** in Docker rather than placing
QEMU inside Docker. It installs the Omarchy runtime/settings payloads, Hyprland,
Quickshell and UWSM, creates a named Hyprland headless output, then streams that
output with Sunshine using NVIDIA NVENC.

It deliberately **does not install Omarchy's machine-level boot stack**
(Limine, Snapper, SDDM). The image installs tiny virtual-provider stubs for
those machine-only dependencies and then installs the normal `omarchy` and
`omarchy-settings` packages. That lets pacman pull Omarchy's complete current
runtime dependency set without giving the container a bootloader, snapshot
manager, or display manager.


## Image profile

The default is:

```ini
OMARCHY_PROFILE=core
```

`core` installs the real Omarchy 4 runtime/settings packages plus the desktop
pieces needed for this use case: Hyprland, Quickshell, UWSM, portals, PipeWire,
Chromium, Foot, Nautilus, clipboard/screenshot tools, fonts, and streaming
helpers. This is the profile to prove first.

After the container is working, you can rebuild with:

```ini
OMARCHY_PROFILE=full
```

`full` additionally installs nearly all packages from Omarchy's current
`install/omarchy-base.packages` list. It filters machine-management packages
that make no sense or are unsafe in a Docker desktop (boot/session manager,
host firewall/network manager, kernel hooks, nested Docker, power/display
hardware controls, and binfmt registration).

The full profile is intentionally not the first-test default: it is much
larger and adds many unrelated variables while debugging the compositor/GPU.

## Local build validation

Build and load the default `core` image with Buildx:

```bash
docker buildx build --load --progress=plain \
  --build-arg OMARCHY_CHANNEL=stable \
  --build-arg OMARCHY_PROFILE=core \
  -t local/omarchy-unraid:test .
```

Then validate its packages, commands, service files, permissions, linked
libraries, and seeded configuration:

```bash
./tests/smoke-image.sh local/omarchy-unraid:test
```

This smoke test does not prove GPU rendering, Wayland capture, NVENC, or input.
Those require starting the container with NVIDIA, DRM, and uinput devices.

## Deployment templates

The development branch has three selectable Compose templates documented in
[Deployment templates](docs/templates.md):

- `headless` uses virtual input only and needs no host seat rule.
- `bridge` reads `/dev/input` read-only and exclusively grabs Sunshine's
  physical event devices through the custom Wayland bridge.
- `seat9` uses the validated private-device path and requires the host seat9
  udev rule.

Choose one template; do not run multiple profiles with the default container
name. For CachyOS development, create the local environment file and select a
GPU:

Create the local environment file and select a GPU:

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
docker compose -f templates/compose.bridge.yml up -d --build
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
  --build-arg OMARCHY_PROFILE=core \
  -t local/omarchy-unraid:latest .
```

Copy the XML into Unraid's user-template directory if it is not being installed
through a template repository:

```bash
cp unraid/omarchy-docker.xml \
  /boot/config/plugins/dockerMan/templates-user/my-omarchy-docker.xml
```

The XML defaults to `br0` and an example dedicated IP. In DockerMan, select an
appropriate custom network and replace the example with an unused IP on that
network. A dedicated IP lets Omarchy use Sunshine's standard ports while a
normal host Sunshine instance uses the Unraid host IP.

The template's **Overview**, **Requires**, and individual field descriptions
document the complete runtime contract. In particular:

- load `uinput` and install the narrow seat9 host rule persistently through
  `/boot/config/go`;
- keep `Privileged` disabled;
- retain the NVIDIA runtime, listed capabilities, cgroup options, ulimits,
  tmpfs mounts, and input cgroup rule from `ExtraParams`;
- map `/dev/dri`, `/dev/uinput`, `/dev/fuse`, `/dev/tty0`, and `/dev/tty1`;
- do not map host `/dev/input`, `/run/udev`, or `/sys/class/input`;
- keep a private/custom network namespace rather than host networking.

See [Input isolation](docs/input-isolation.md) for the persistent Unraid host
rule commands and verification sequence.

Sunshine uses its normal base port 47989 on the dedicated IP. Its web UI is:

```text
https://<DEDICATED-IP>:47990
```

Create the Sunshine web UI account, pair Moonlight, then launch **Omarchy
Desktop**.

This configuration was validated with CachyOS KDE and an RDP session still
active. Moonlight keyboard, pointer motion, and clicks reached Omarchy without
controlling the host desktop after the seat rule was installed and the
container was restarted. Both Sunshine mouse devices were attached to
Hyprland.

If Sunshine is accessed through a non-default hostname or address, add its
complete browser origin to `.env` on CachyOS or the XML variable on Unraid:

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

Runtime defaults (`.env` on CachyOS and XML fields on Unraid):

```ini
OMARCHY_OUTPUT_NAME=OMARCHY
OMARCHY_RESOLUTION=2560x1440
OMARCHY_REFRESH=60
OMARCHY_SCALE=1
```

Hyprland supports named headless outputs and explicitly documents them for
VNC/RDP/Sunshine use.

## GPU / DRM selection

Do not set `AQ_DRM_DEVICES` initially.

If Hyprland chooses the wrong card on a multi-GPU Unraid host, identify the DRM
card corresponding to the selected GPU and set, for example:

```ini
AQ_DRM_DEVICES=/dev/dri/card1
```

Aquamarine uses `AQ_DRM_DEVICES` as an explicit DRM device list.

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

The Compose templates and Unraid XML template persist:

```text
/mnt/user/appdata/omarchy-docker/home      -> /home/omarchy
/mnt/user/appdata/omarchy-docker/sunshine  -> /config
```

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

This is intentionally a prototype with strong diagnostics rather than a black
box image.

## Development roadmap

The staged validation and hardening plan is in [ROADMAP.md](ROADMAP.md). It
also records the container, NVIDIA, Sunshine, input, and audio lessons carried
over from the related `remominor/steam-wayland` work while keeping this image
native to Arch, Omarchy, Hyprland, UWSM, and systemd.
