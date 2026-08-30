# Deployment templates

The development branch provides three runtime profiles. They share one
Dockerfile and the same pinned CUDA-enabled Sunshine package; the Compose file
selects the input topology.

| File | Use when | Host input requirement | Special mapping |
|---|---|---|---|
| `templates/compose.headless.yml` | Virtual input is sufficient | None | `/run/udev` read-only; host networking |
| `templates/compose.bridge.yml` | Physical keyboard/mouse must be grabbed exclusively | None | `/dev/input` read-only at `/host/input` |
| `templates/compose.seat9.yml` | Existing seat9 isolation is preferred | Install `host/72-omarchy-sunshine-seat.rules` | No host `/dev/input` mapping |

All profiles persist `/home/omarchy` and `/config` through host bind mounts.
Compose defaults to the repository-relative `../appdata/omarchy-docker` path
(relative to `templates/`); set `APPDATA_PATH` in `.env` for another local path.
The Unraid XML intentionally keeps its `/mnt/user/appdata/...` defaults.

The default capability set retains `MKNOD` because the seat9 profile creates
private event nodes with `omarchy-sync-input-nodes`. The bridge and headless
profiles do not use `MKNOD`; it can be removed in a deployment that never uses
seat9, but is left enabled by default so the shared image remains selectable.
Only the bridge profile reads host event nodes. Only seat9 runs the virtual
event-node synchronization helper. The headless profile uses neither.

Start one profile at a time:

```bash
docker compose -f templates/compose.headless.yml up -d --build
docker compose -f templates/compose.bridge.yml up -d --build
docker compose -f templates/compose.seat9.yml up -d --build
```

## Sunshine networking

The bridge and seat9 templates publish Sunshine's standard ports. If the host
already runs Sunshine, place the container on a custom Docker network with its
own LAN-reachable IP (for example a macvlan/ipvlan or Unraid custom network)
and keep the standard ports on that IP. Changing Sunshine to arbitrary host
ports is not a reliable workaround because some Moonlight clients expect the
standard port set. Host networking is appropriate for the headless template
only when no other Sunshine instance needs those ports.

## Useful Omarchy checks

For broader Omarchy smoke testing, rebuild with `OMARCHY_PROFILE=full`, then
exercise the desktop from Moonlight: Quickshell panels, terminal, Chromium,
Nautilus, clipboard, screenshots, audio, suspend/reconnect behavior, and
Sunshine's NVENC stream. Use `omarchy-container-check` before and after each
change so rendering, user services, audio, and input failures stay distinct.

## Follow-up feature checklist

Use this checklist after monthly image rebuilds or when changing the desktop
profile. It is intentionally manual and is not a bridge acceptance gate.

- [ ] Start each selected profile and confirm its health status becomes
  `healthy`.
- [ ] Confirm the named Hyprland output has the requested resolution, refresh,
  and scale.
- [ ] Open Quickshell panels, Foot, Chromium, and Nautilus through Moonlight.
- [ ] Verify clipboard copy/paste, screenshots, notifications, and file dialogs.
- [ ] Verify PipeWire audio reaches Sunshine and survives a reconnect.
- [ ] Confirm Sunshine reports NVENC rather than software encoding.
- [ ] Reconnect Moonlight repeatedly and check for stale sessions or stuck
  keyboard/pointer buttons.
- [ ] For `bridge`, run `omarchy-container-check` and confirm `/host/input` is
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

Flatpak is available in the image. Install applications as the `omarchy` user
so they persist in the home bind mount:

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

Monthly Omarchy releases should produce a new image from `main`; live CVE
patches can be installed for testing, then incorporated into the next image
rebuild. The `main` workflow publishes the headless image as both `latest` and
`headless` to GHCR.
