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
Those require starting the privileged container with NVIDIA and DRM devices.

## Recommended first test

Use a different NVIDIA GPU from Steam Headless for the first boot. Once this is
working, sharing one GPU is a separate test.

On Unraid:

```bash
mkdir -p /mnt/user/appdata/omarchy-docker/build
cd /mnt/user/appdata/omarchy-docker/build
# Copy this project here, then:
cp .env.example .env
nano .env
```

Find your NVIDIA GPU UUIDs:

```bash
nvidia-smi --query-gpu=index,name,uuid,pci.bus_id --format=csv
```

Put the UUID of the GPU you want into:

```ini
NVIDIA_VISIBLE_DEVICES=GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

## Networking: recommended dedicated IP

Because Steam Headless is already likely using Sunshine/GameStream's default
port family, the cleanest Unraid setup is giving this container its **own IP**.

Edit:

```ini
DOCKER_NETWORK=br0
OMARCHY_IP=192.168.1.250
```

Use an unused IP appropriate for that Unraid Docker network/VLAN, then:

```bash
docker compose -f compose-unraid-ip.yml build --no-cache
docker compose -f compose-unraid-ip.yml up -d
docker logs -f omarchy
```

Sunshine uses its normal base port 47989 on that dedicated IP. Its web UI is:

```text
https://<OMARCHY_IP>:47990
```

Create the Sunshine web UI account, pair Moonlight, then launch **Omarchy
Desktop**.

## Host-network fallback

If you do not want a dedicated Docker IP:

```bash
docker compose -f compose-host.yml build --no-cache
docker compose -f compose-host.yml up -d
```

This defaults Sunshine's base port to **48989**, making the web UI:

```text
https://<UNRAID-IP>:48990
```

The rest of Sunshine's port family shifts by the same +1000 offset.

**Caveat:** Sunshine documents that custom ports may not work with every
Moonlight client. That is why the dedicated-IP compose file is preferred.


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

`.env` defaults:

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

The compose files persist:

```text
/mnt/user/appdata/omarchy-docker/home      -> /home/omarchy
/mnt/user/appdata/omarchy-docker/sunshine  -> /config
```

The image copies Omarchy's `/etc/skel` defaults into an empty persistent home
on first boot. Existing files are not overwritten. A dedicated user service
creates the headless output after UWSM begins launching Hyprland.

If you want a totally clean retry:

```bash
docker compose -f compose-unraid-ip.yml down
rm -rf /mnt/user/appdata/omarchy-docker/home
rm -rf /mnt/user/appdata/omarchy-docker/sunshine
docker compose -f compose-unraid-ip.yml up -d
```

## Security note

This prototype intentionally runs `privileged` and gives the container user
passwordless sudo/polkit authorization. **Treat the container as root-equivalent
access to the Unraid host's hardware while testing it.** In particular, a
privileged container can see host device nodes, so do not use disk/firmware/
power-management utilities from inside it.

That broad access is deliberate for the first GPU/Wayland proof-of-concept.
Once it works reliably, the next step should be reducing it to explicit
NVIDIA/DRM/uinput/audio devices and capabilities and removing `privileged`.

## Expected first failure points

If it does not come up immediately, the most useful distinction is:

1. **Hyprland does not start** — inspect `journalctl` and `AQ_DRM_DEVICES`.
2. **Hyprland starts but renderer is llvmpipe** — NVIDIA EGL/GBM visibility is
   the next thing to fix.
3. **Hyprland + Quickshell work but Sunshine cannot capture** — inspect
   `WAYLAND_DISPLAY`, the `wlr` capture log, and the named `OMARCHY` output.
4. **Sunshine captures but NVENC fails** — NVIDIA `video` capability or driver
   library injection is the problem.
5. **Everything works on its own but conflicts with Steam Headless** — then
   test same-GPU coexistence separately; do not change three variables at once.

This is intentionally a prototype with strong diagnostics rather than a black
box image.

## Development roadmap

The staged validation and hardening plan is in [ROADMAP.md](ROADMAP.md). It
also records the container, NVIDIA, Sunshine, input, and audio lessons carried
over from the related `remominor/steam-wayland` work while keeping this image
native to Arch, Omarchy, Hyprland, UWSM, and systemd.
