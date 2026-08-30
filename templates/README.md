# Omarchy Docker input templates

Choose exactly one compose template for the host's input topology:

| Template | Input path | Host requirement |
|---|---|---|
| `compose.headless.yml` | Sunshine virtual input only | No host input devices or seat rule |
| `compose.bridge.yml` | Custom Wayland bridge with exclusive evdev grabs | Read-only `/dev/input` mount |
| `compose.seat9.yml` | Sunshine evdev nodes mirrored into private `/dev` | Host `seat9` udev rule |

Run one with `docker compose -f templates/compose.<mode>.yml up -d --build`.
All templates use the same pinned CUDA-enabled Sunshine package. Do not run
more than one template at a time with the default container name.
