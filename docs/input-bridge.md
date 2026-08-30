# Omarchy Docker input bridge — prototype closeout

Status: **functionally complete prototype** on the `dev` branch.

The `bridge` Compose profile reads host evdev nodes through `/host/input`,
exclusively grabs only this container's Sunshine devices, and injects keyboard
and mouse events into Hyprland through Wayland virtual-input protocols.

## Implemented requirements

- Sunshine alone uses `XDG_SEAT=seat9`; no host udev rule, daemon, systemd unit,
  or global seat change is required by bridge mode.
- Host `/dev/input` is mounted read-only only at `/host/input`; it is never
  mounted at container `/dev/input`.
- Bridge mode does not mount host `/run/udev` or `/sys/class/input`, and does
  not start fake-udev or `omarchy-sync-input-nodes`.
- Matching requires the exact Sunshine name, identity `0xBEEF:0xDEAD:0x0111`,
  and expected keyboard/relative/absolute capabilities. Physical devices and
  unsuffixed Sunshine devices are ignored.
- `EVIOCGRAB` succeeds before activation; grabs release on teardown/removal.
- Keyboard, modifiers, relative and absolute motion, buttons, wheel, and
  high-resolution wheel events forward. Kernel repeat events are not duplicated.
- inotify plus a five-second fallback rescan handles device lifecycle without
  discovery work in the motion loop.
- Wayland preflight requires `wl_seat`, virtual keyboard, and virtual pointer
  globals, with no fake-udev fallback.
- Readiness is published at `/run/omarchy-input-bridge/ready` only after the
  Wayland objects and device monitor are live.
- Systemd stops Sunshine when the bridge exits or loses Wayland, then can
  recover through service restart.
- Logs contain state and device identity, not raw key contents.
- `omarchy-container-check` reports service state, process identity, mounts,
  readiness, compositor environment, and recent bridge errors/grabs.
- Gamepads, touchscreens, tablets, and other non-keyboard/mouse devices remain
  out of scope.

## Validation completed

The bridge was exercised on the active CachyOS/KDE host with the host seat9
rule disabled. Moonlight keyboard, pointer motion, clicks, dragging, and
scrolling reached Omarchy without driving the host desktop. Pointer smoothness
was corrected by moving discovery out of the motion path. The container stayed
non-privileged and the normal display/capture path remained intact.

The bridge binary builds from pinned Wayland protocol inputs, and all three
Compose templates and shell scripts validate.

## Deferred, non-blocking hardening

No capability change is required to close this prototype. `MKNOD` is not used
by the bridge itself, but remains in the shared seat9 runtime because that mode
creates private event nodes. `SYS_ADMIN` remains for the existing desktop and
`/dev/fuse` runtime. `NET_RAW`, `NET_ADMIN`, `SYS_NICE`, `IPC_LOCK`, and
`SYS_TTY_CONFIG` remain as existing runtime settings whose bridge-specific
necessity has not been isolated.

If hardening becomes a goal, test a bridge-only profile with `MKNOD` removed
first, then remove other capabilities one at a time while checking Hyprland,
Sunshine/NVENC, audio, and reconnect behavior. Do not change seat9 as part of
that experiment.

The bridge currently runs as root inside a non-privileged container so it can
open host event nodes regardless of numeric host input-group IDs. A dedicated
bridge user or file capabilities can be evaluated later, but is not needed for
the prototype.

Automated host-isolation tests are intentionally not required. Future changes
to lifecycle or input code should manually recheck bridge failure/restart and
normal host Sunshine regression.

## Reference and rollback

The implementation follows [Prism](https://github.com/atgehrhardt/prism),
especially its evdev identity checks, `EVIOCGRAB`, and Wayland virtual
keyboard/pointer forwarding. Prism is a behavioral reference, not a vendored
dependency.

The `seat9-input` branch remains the rollback/reference implementation. The
bridge and seat9 profiles coexist in the shared image; selecting
`compose.bridge.yml` activates the bridge path.
