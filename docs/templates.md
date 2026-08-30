# Deployment templates

The development branch provides three runtime profiles. They share one
Dockerfile and the same pinned CUDA-enabled Sunshine package; the Compose file
selects the input topology.

| File | Use when | Host input requirement | Special mapping |
|---|---|---|---|
| `templates/compose.headless.yml` | Virtual input is sufficient | None | `/run/udev` read-only; host networking |
| `templates/compose.bridge.yml` | Physical keyboard/mouse must be grabbed exclusively | None | `/dev/input` read-only at `/host/input` |
| `templates/compose.seat9.yml` | Existing seat9 isolation is preferred | Install `host/72-omarchy-sunshine-seat.rules` | No host `/dev/input` mapping |

All profiles persist `/home/omarchy` and `/config` through host bind mounts. Set
`APPDATA_PATH` in `.env` before starting.
Only the bridge profile reads host event nodes. Only seat9 runs the virtual
event-node synchronization helper. The headless profile uses neither.

Start one profile at a time:

```bash
docker compose -f templates/compose.headless.yml up -d --build
docker compose -f templates/compose.bridge.yml up -d --build
docker compose -f templates/compose.seat9.yml up -d --build
```

## Persistence and updates

The home bind mount is the durable location for user configuration and
per-user application installs such as Flatpak. Package-manager changes made
with `pacman` modify the writable container layer and are lost on recreation;
bake those changes into a rebuild or use a per-user installer under
`/home/omarchy` when available.

Flatpak is not currently part of the core image. If it is added later, keep
user installations under the home bind mount. Do not add a `/proc` bind mount:
Docker already provides proc, and Flatpak setup must tolerate an existing proc
mount (`mountpoint -q /proc || mount -t proc none /proc`). The `/dev/fuse`,
`SYS_ADMIN`, and unconfined seccomp settings already present in the non-headless
profiles are the relevant prerequisites.

Monthly Omarchy releases should produce a new image from `main`; live CVE
patches can be installed for testing, then incorporated into the next image
rebuild. The `main` workflow publishes the headless image as both `latest` and
`headless` to GHCR.
