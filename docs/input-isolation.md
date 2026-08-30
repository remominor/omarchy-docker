# Sunshine input isolation

Omarchy uses two different udev views of the same Sunshine-created kernel
input devices. This keeps Moonlight input usable inside Hyprland without
letting the host's seat0 desktop consume it.

```text
Moonlight
  -> Sunshine in the Omarchy container (XDG_SEAT=seat9)
  -> /dev/uinput creates Keyboard/Mouse passthrough (seat9)
       -> host udev: ID_SEAT=seat9 -> host seat0 ignores it
       -> private container udev: ID_SEAT=seat0 -> seatd/Hyprland uses it
```

`SUNSHINE_SEAT=seat9` is passed only to the container's Sunshine wrapper.
Hyprland and seatd remain on seat0. Do not export `XDG_SEAT=seat9` globally,
put it in the host desktop environment, or add it to the host Sunshine service.

Normal host Sunshine remains safe because its seat0 devices have unsuffixed
names such as `Keyboard passthrough`. The host rule below matches only devices
whose names contain `(seat9)`.

## CachyOS host setup

Install the tracked rule on the Docker host:

```bash
sudo install -m 0644 \
  host/72-omarchy-sunshine-seat.rules \
  /etc/udev/rules.d/72-omarchy-sunshine-seat.rules

sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input
```

Then restart the Omarchy container:

```bash
docker restart omarchy
```

The restart is important when the devices existed before the rule was
installed. Changing udev metadata does not revoke input descriptors that a
host compositor may already have opened. Recreating the Sunshine devices
ensures they are seat9 from their first host udev event.

This persistent rule does not prevent normal host Sunshine from working after
Omarchy is stopped. It has no effect on unsuffixed host Sunshine devices,
physical input devices, KDE, KRDP, or the host's global seat configuration.

## Unraid host setup

Unraid's root filesystem is recreated at boot, so a rule installed only under
`/etc/udev/rules.d` is temporary. Store the tracked rule on the boot device:

```bash
mkdir -p /boot/config/omarchy-docker
cp host/72-omarchy-sunshine-seat.rules \
  /boot/config/omarchy-docker/72-omarchy-sunshine-seat.rules
```

Add these commands to `/boot/config/go` before the command that starts Docker
containers, if one exists:

```bash
modprobe uinput
install -m 0644 \
  /boot/config/omarchy-docker/72-omarchy-sunshine-seat.rules \
  /etc/udev/rules.d/72-omarchy-sunshine-seat.rules
udevadm control --reload-rules
```

Run those same three commands once for the current boot, confirm
`/dev/uinput` exists, then start or restart the Omarchy container from
DockerMan. The rule must be loaded before the new Sunshine event devices are
created.

Use [the Unraid XML template](../unraid/omarchy-docker.xml) with a custom
network and dedicated IP. Do not switch it to host networking or add host
input/udev mounts.

## Runtime design

Use `compose.seat9.yml` on CachyOS or the Unraid XML template. Both run
non-privileged, have a private network namespace and private `/run`, and
intentionally omit host `/dev/input`, `/run/udev`, and `/sys/class/input` bind
mounts.

The `omarchy-sync-input-nodes` helper watches the container-visible sysfs for
these exact names:

```text
Keyboard passthrough (seat9)
Mouse passthrough (seat9)
Mouse passthrough (seat9) (absolute)
```

For each matching event device it creates the private `/dev/input/eventN`,
writes `/run/udev/data/cMAJOR:MINOR` with `ID_SEAT=seat0`, and sends a synthetic
GROUP_UDEV add event through `fake-udev`. On removal it emits remove and deletes
the private record and node. Container `systemd-udevd` is masked so it cannot
race this private view.

The private network namespace matters because userspace udev notifications use
netlink. It keeps the synthetic container hotplug event out of the host while
the real kernel device remains globally visible and assigned to host seat9.

## Verification

After reconnecting Moonlight, verify the host view:

```bash
for d in /sys/class/input/input*; do
  name=$(cat "$d/name" 2>/dev/null || true)
  case "$name" in
    *"(seat9)"*)
      printf '%s -> %s\n' "$d" "$name"
      udevadm info --query=property --path="$d" | grep '^ID_SEAT='
      ;;
  esac
done
```

There should be three matching devices and each should report
`ID_SEAT=seat9`.

Verify the private container view:

```bash
docker exec omarchy sh -lc '
  for node in /dev/input/event*; do
    printf "%s " "$node"
    udevadm info --query=property --name="$node" | \
      grep -E "^(ID_SEAT|ID_INPUT_MOUSE|ID_INPUT_KEYBOARD)=" | \
      tr "\n" " "
    echo
  done
'
```

The three nodes should report `ID_SEAT=seat0` and the appropriate mouse or
keyboard classification. `hyprctl devices` should list both passthrough mice
and the passthrough keyboard.

The final behavioral result is:

```text
Moonlight input -> Omarchy: yes
Moonlight input -> host desktop: no
Normal host Sunshine after Omarchy stops: unchanged
```

## Moonlight client keyboard behavior

On the macOS Moonlight client used for validation, **Capture system keyboard
shortcuts** had to be enabled before Omarchy's Super-key shortcuts reached the
container. That client maps the Mac Command key to Super, which works for
Omarchy shortcuts.

This is client-side behavior rather than a container input failure. Shortcut
capture and modifier mapping may differ on Windows, Linux, mobile, and other
Moonlight clients.

## Troubleshooting boundary

- If `evtest` cannot read events from the private node, debug node creation,
  major/minor values, and permissions.
- If `evtest` works but the device is absent from Hyprland, inspect the private
  udev record and fake-udev add event.
- If Omarchy and the host both react, verify host `ID_SEAT=seat9` and restart
  Omarchy after loading the rule.
- If the host still reacts while no host process has the seat9 event nodes
  open, investigate a second remote-input path such as the client or RDP rather
  than broadening container device access.

## References

- [Sunshine multiseat troubleshooting](https://docs.lizardbyte.dev/projects/sunshine/master/md_docs_2troubleshooting.html)
- [Games on Whales: Hotplug in Docker](https://games-on-whales.github.io/wolf/stable/dev/fake-udev.html)
