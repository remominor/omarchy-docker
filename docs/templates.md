# Deployment templates

The repository provides three input templates. They share the same image and
pinned CUDA-enabled Sunshine package; each Compose file selects a different
input topology. These input templates are independent of the `core`, `full`,
and `gaming` image variants.

| File                   | Use when                                            | Host input requirement                        | Special mapping                                 |
| ---------------------- | --------------------------------------------------- | --------------------------------------------- | ----------------------------------------------- |
| `compose.headless.yml` | Virtual input is sufficient                         | None                                          | `/run/udev` read-only; published Sunshine ports |
| `compose.bridge.yml`   | Physical keyboard/mouse must be grabbed exclusively | None                                          | `/dev/input` read-only at `/host/input`         |
| `compose.seat9.yml`    | Existing seat9 isolation is preferred               | Install `host/72-omarchy-sunshine-seat.rules` | No host `/dev/input` mapping                    |

All input templates persist `/home/omarchy` and `/config` through host bind mounts.

Compose defaults to the repository-relative `./appdata/omarchy-docker` path. Set `APPDATA_PATH` in `.env` to use another local path.

The Unraid XML intentionally keeps its `/mnt/user/appdata/...` defaults.

The default capability set retains `MKNOD` because the seat9 profile creates private event nodes with `omarchy-sync-input-nodes`. The bridge and headless profiles do not use `MKNOD`; it can be removed in a deployment that never uses seat9, but is left enabled by default so the shared image remains selectable.

Only the bridge profile reads host event nodes. Seat9 and headless run the virtual event-node discovery helper. In headless mode it only exposes Sunshine-created devices and never reads or grabs host input.

Start one profile at a time:

```bash
docker compose --env-file .env -f compose.headless.yml up -d --build
docker compose --env-file .env -f compose.bridge.yml up -d --build
docker compose --env-file .env -f compose.seat9.yml up -d --build
```

## Sunshine networking

All Compose templates publish Sunshine's standard ports.

If the host already runs Sunshine, create a custom Docker network with its own LAN-reachable IP, such as macvlan/ipvlan or an Unraid custom network, and attach the container to it. The standard Sunshine ports are then available directly on that IP without colliding with host services.

A user-defined bridge network still requires the listed port mappings.

Changing Sunshine to arbitrary host ports is not a reliable workaround because some Moonlight clients expect the standard port set.

## Useful Omarchy checks

For broader Omarchy smoke testing, build `dockerfile.full` over the core image,
then exercise the desktop from Moonlight:

```bash
docker build -t local/omarchy-core:dev .
docker build -f dockerfile.full \
  --build-arg BASE_IMAGE=local/omarchy-core:dev \
  -t local/omarchy-full:test .
```

* Quickshell panels
* Foot
* Chromium
* Nautilus
* clipboard
* screenshots
* notifications and file dialogs
* PipeWire audio
* reconnect behavior
* Sunshine NVENC capture
* Steam and Proton when testing gaming images

Use `omarchy-container-check` before and after each change so rendering, user services, audio, input, and gaming failures remain distinct.

## Follow-up feature checklist

Use this checklist after monthly image rebuilds or when changing the desktop profile. It is intentionally manual and is not a bridge acceptance gate.

* [x] Start the bridge profile and confirm its health status becomes `healthy`.
* [x] Confirm the named Hyprland output has the requested resolution, refresh, and scale.
* [x] Open Quickshell panels, Foot, Chromium, and Nautilus through Moonlight.
* [x] Verify clipboard copy/paste, screenshots, notifications, and file dialogs.
* [x] Verify PipeWire audio reaches Sunshine and survives a reconnect.
* [x] Confirm Sunshine reports NVENC rather than software encoding.
* [x] If SSH is enabled, recreate the container and confirm the authorized key still works and `sshd` starts automatically; containers without an authorized key must remain SSH-disabled.
* [x] Reconnect Moonlight repeatedly and check for stale sessions or stuck keyboard/pointer buttons.
* [x] Confirm startup logs report `/dev/uinput is available`; if it is missing, Sunshine cannot create virtual keyboard, mouse, or gamepad devices.
* [x] With a controller connected, confirm the Sunshine gamepad event node appears under `/dev/input` and is visible to Steam.
* [x] For `bridge`, run `omarchy-container-check` and confirm `/host/input` is read-only and bridge readiness is published.
* [x] Flatpak Steam launches native Linux games on the validated CachyOS/NVIDIA configuration.
* [x] Flatpak Steam launches Proton games on the validated CachyOS/NVIDIA configuration when the outer container does not contain a native 32-bit graphics stack.
* [ ] Repeat the Flatpak Proton validation on Unraid using a clean image without native 32-bit graphics packages.
* [ ] Validate native Steam/Proton on Unraid with the corrected host-matched compat32 CDI path.
* [ ] For `seat9`, verify the host rule is installed and host input remains isolated.
* [ ] Exercise additional Omarchy applications included by `dockerfile.full` before promoting the rebuilt image.

## Persistence and updates

The home bind mount is the durable location for user configuration and per-user application data.

Omarchy's preferred software path remains its native Arch/Omarchy packages and AUR where appropriate. Package-manager changes made with `pacman` modify the writable container layer and are lost on recreation, so system package changes should be incorporated into the Dockerfile and image rebuild.

Use a per-user installer under `/home/omarchy` when an application supports it.

### Future user bootstrap hook

If recurring setup becomes necessary, a future enhancement could run scripts from:

```text
/home/omarchy/.config/omarchy-container/bootstrap.d/
```

during session startup.

That hook should be limited to user-level configuration and installs such as `~/.local` tools.

Flatpak is available for applications that benefit from a self-contained runtime or are not available through Omarchy's normal native package path.

The hook must not become a root package installation mechanism. System packages belong in the Dockerfile/profile and the normal image rebuild process.

## Steam installation models

Steam is unusual in an NVIDIA container because native Steam/Proton and Flatpak Steam do not have the same graphics-library ownership model.

Testing demonstrated that these two paths should be treated separately.

The general image should not assume that adding more native 32-bit graphics libraries improves Flatpak compatibility. On NVIDIA, doing so can make Flatpak Proton less reliable by exposing an outer `/usr/lib32` graphics stack to Steam's pressure-vessel runtime.

### Flatpak Steam

Flatpak Steam supplies its own NVIDIA GL and GL32 runtime extensions matching the active host driver.

A Flatpak-focused Omarchy image therefore does **not** need a native 32-bit NVIDIA graphics stack in the outer container.

For maximum isolation, the Flatpak-focused path should avoid installing native outer-container packages such as:

```text
lib32-libglvnd
lib32-mesa
lib32-nvidia-utils
lib32-nvidia-580xx-utils
```

unless another explicitly supported application requires them.

The successful A/B test retained `lib32-vulkan-icd-loader`. The standalone
loader is therefore not part of the confirmed conflict. The regression was cleared by removing
`lib32-libglvnd` and `lib32-mesa` while leaving the loader installed.

This is not because those packages are inherently broken. The issue is that they create a second native 32-bit graphics environment in the outer container. Flatpak Steam's pressure-vessel runtime can discover or interact with that environment instead of relying exclusively on Flatpak's matching NVIDIA runtime.

The validated ownership model is therefore:

```text
Host NVIDIA driver
        |
        +-- 64-bit NVIDIA runtime required by the Omarchy desktop
        |
        +-- Flatpak NVIDIA GL extension
        |
        +-- Flatpak NVIDIA GL32 extension
                    |
                    +-- Flatpak Steam
                          |
                          +-- Proton / DXVK / VKD3D
```

The outer Omarchy container does not need to supply the Proton process with its own native 32-bit NVIDIA stack.

In the general image, install persistent Flatpak Steam as the `omarchy` user:

```bash
docker exec -it omarchy bash -lc \
  'runuser -u omarchy -- flatpak --user install flathub com.valvesoftware.Steam'
```

Do not install user applications with root `flatpak install`.

The gaming image is intentionally different: it installs Flatpak Steam at the
system level during the image build. The application is reproduced by image
rebuilds, while Steam configuration, downloaded client state, compatibility
tools, prefixes, and game data under `/home/omarchy` remain persistent through
the home bind mount.

Do not add a `/proc` bind mount. Docker already provides proc, and Flatpak setup must tolerate the existing proc mount.

The `/dev/fuse`, `SYS_ADMIN`, and unconfined seccomp settings already present in the applicable deployment profiles are the relevant Flatpak/container prerequisites.

### Native Steam

Native Steam is fundamentally different.

Steam and Proton contain native 32-bit processes. Those processes need a usable 32-bit GL/Vulkan environment in the outer container.

For NVIDIA, the vendor-specific 32-bit libraries must match the host kernel driver and the injected 64-bit NVIDIA userspace.

The intended ownership model for native Steam is:

```text
Host NVIDIA driver
        |
        +-- host-matched 64-bit NVIDIA userspace
        |      supplied by NVIDIA Container Toolkit
        |
        +-- host-matched 32-bit NVIDIA userspace
               supplied by a correct compat32 runtime/CDI path
                    |
                    +-- native Steam / Proton
```

Installing whatever `lib32-nvidia-*` package happens to be current in an Arch repository is not portable. The repository package can belong to a different NVIDIA branch than the active host driver.

The image must not bake a fixed NVIDIA driver version merely to make native Steam work.

## NVIDIA 32-bit graphics findings

The NVIDIA testing occurred in two different host environments and should not be conflated:

1. General Linux Docker on an Arch-based CachyOS host.
2. Unraid using its NVIDIA plugin and an experimental compat32 CDI workaround.

The findings overlap, but the host library layouts and runtime behavior differ.

# General Linux / Arch-based host validation

The primary general-Linux validation host is CachyOS with NVIDIA.

## Initial native Steam failure

Testing on host driver:

```text
610.57.04
```

showed that NVIDIA Container Toolkit supplied the active host driver's 64-bit userspace to the container, but the released JIT-CDI path did not provide the matching 32-bit NVIDIA stack.

When native Steam was installed through Pacman, dependency resolution selected:

```text
lib32-nvidia-580xx-utils 580.178.04
```

The resulting environment mixed:

```text
64-bit NVIDIA userspace: 610.57.04
32-bit NVIDIA userspace: 580.178.04
host kernel driver:      610.57.04
```

`glxinfo32 -B` then failed with `X_GLXCreateNewContext` / `BadValue`, and native Steam crashed after starting its updater.

Replacing the 580-series 32-bit userspace in a disposable test with libraries matching the host's `610.57.04` driver made the 32-bit GLX path work and allowed native Steam to open.

This confirmed that the original native Steam failure was a driver-userspace version mismatch rather than a fundamental Hyprland or XWayland limitation.

## JIT-CDI compat32 omission

The missing host-matched 32-bit stack was traced to NVIDIA Container Toolkit's JIT-CDI discovery.

Toolkit `1.18.0` changed `auto` mode from the previous legacy injection behavior to just-in-time CDI.

The released CDI locator reads the host linker cache but does not include its 32-bit result in the generated driver library set, even when `NVIDIA_DRIVER_CAPABILITIES=all` requests `compat32`.

The legacy:

```bash
nvidia-container-cli --compat32
```

path can still discover the same host 32-bit libraries.

NVIDIA PR `#2035`, which supersedes `#1968`, adds the omitted 32-bit linker-cache entries to CDI discovery and explicitly targets workloads such as Steam, Proton, 32-bit Vulkan, CUDA, and VDPAU.

A patched CDI generator based on that work was tested against the CachyOS host running `610.57.04`.

The released generator produced no compat32 mounts. The patched generator discovered and emitted the matching 32-bit NVIDIA libraries, including examples such as:

```text
libGLX_nvidia
libcuda
libnvidia-glcore
libnvidia-tls
```

This verified the upstream discovery problem independently of Steam.

## Flatpak control test

Flatpak Steam provided an important control experiment.

The previously working Omarchy full/bridge image did **not** contain a native 32-bit graphics stack in the outer container.

It contained the normal 64-bit graphics packages:

```text
libglvnd
mesa
vulkan-icd-loader
```

but not the corresponding native `lib32-*` graphics packages.

In this configuration the following Flatpak components were used:

```text
Steam:                     1.0.0.85
Freedesktop Platform:      25.08
NVIDIA GL extension:       610.57.04
NVIDIA GL32 extension:     610.57.04
```

Flatpak Steam successfully launched:

* native Linux games
* Proton games
* DXVK/Vulkan workloads

This demonstrated that Flatpak Proton did not require a native host-matched compat32 stack in the outer Omarchy container.

## Generic native lib32 regression

A later gaming image added a generic native 32-bit graphics stack through packages such as:

```text
lib32-libglvnd
lib32-mesa
lib32-vulkan-icd-loader
```

`lib32-libglvnd` also brought in the corresponding generic OpenGL provider dependencies.
The later A/B removal retained `lib32-vulkan-icd-loader`; only
`lib32-libglvnd` and `lib32-mesa` were removed.

The Flatpak Steam installation itself was unchanged:

```text
Steam:                     1.0.0.85
Freedesktop Platform:      25.08
NVIDIA GL extension:       610.57.04
NVIDIA GL32 extension:     610.57.04
```

Despite the identical Flatpak runtime, behavior changed:

```text
Old image without outer native lib32 graphics:
    Flatpak native Linux games: PASS
    Flatpak Proton/DXVK:        PASS

Gaming image with outer native lib32 graphics:
    Flatpak native Linux games: PASS
    Flatpak Proton/DXVK:        FAIL
```

The failure included Vulkan/FNA3D presentation errors from Proton workloads.

Removing the native outer-container `lib32-libglvnd` and `lib32-mesa`
packages restored Flatpak Proton functionality on CachyOS.

This confirms that the Flatpak Proton regression was caused by the additional native 32-bit graphics environment rather than by:

* the game
* the physical GPU
* Flatpak Steam version
* Freedesktop runtime version
* NVIDIA Flatpak extension version

The likely mechanism is that Steam's pressure-vessel runtime can discover or interact with the outer container's `/usr/lib32` graphics stack instead of exclusively using the matching Flatpak NVIDIA runtime.

The durable rule for the Flatpak-focused image is therefore:

> Do not add a native outer-container 32-bit GL/Vulkan stack merely for Flatpak Steam.

## CachyOS validated configurations

### Flatpak-focused NVIDIA configuration

Validated:

```text
Outer container:
    64-bit graphics stack only
    no native lib32 graphics stack required by Steam

Flatpak:
    host-matched NVIDIA GL
    host-matched NVIDIA GL32

Results:
    Steam UI:          PASS
    native Linux game: PASS
    Proton game:       PASS
```

### Native Steam configuration

Native Steam requires a separate configuration:

```text
Outer container:
    native 32-bit loaders
    host-matched NVIDIA compat32 vendor libraries
```

A random repository `lib32-nvidia-*` package must not be used as a substitute for host matching.

# Unraid NVIDIA validation

Unraid differs from the CachyOS host because the NVIDIA plugin stores its native and compatibility libraries in a host filesystem layout that does not directly match Arch's container layout.

The Unraid testing host currently uses:

```text
NVIDIA driver 595.99.02
```

## Experimental compat32 workaround

The Unraid NVIDIA plugin compat32 experiment uses:

```bash
nvidia-container-cli list --libraries --compat32
```

to discover the host's actual ELF32 NVIDIA libraries and adds them to the generated CDI specification.

This solves the first half of the problem: the discovered files actually come from the active Unraid NVIDIA driver rather than from an unrelated Arch package.

However, directly mirroring host paths into the container exposed additional portability problems.

## Arch `/usr/lib32` destination translation

On Unraid:

```text
64-bit NVIDIA libraries: /usr/lib64
32-bit NVIDIA libraries: /usr/lib
```

On Arch:

```text
/usr/lib64 -> /usr/lib
32-bit libraries -> /usr/lib32
```

Mounting Unraid's host ELF32 `/usr/lib/...` paths unchanged into an Arch container causes them to collide with Arch's native 64-bit `/usr/lib`.

The experimental Unraid CDI specification therefore needs architecture-aware destination translation:

```text
Unraid host:
/usr/lib/<32-bit NVIDIA library>

Arch container:
/usr/lib32/<32-bit NVIDIA library>
```

The source path remains the host's actual Unraid library. Only the container destination changes.

## Generic GL compatibility aliases

The first translated CDI test also exposed a second portability problem.

The host library discovery code included symlink aliases that resolved to NVIDIA libraries. One of these was:

```text
libGLX_indirect.so.0
```

When copied into the Arch container as:

```text
/usr/lib32/libGLX_indirect.so.0
```

it collided with `lib32-mesa`.

Arch legitimately owns this generic compatibility alias:

```text
/usr/lib32/libGLX_indirect.so.0
    -> libGLX_mesa.so.0.0.0
```

The file is owned by:

```text
lib32-mesa
```

The corrected Unraid experiment therefore excludes `libGLX_indirect.so.0` from the NVIDIA compat32 mounts.

This establishes an important rule:

> Runtime compat32 injection should supply NVIDIA vendor-specific libraries, not blindly copy every generic host GL/GLVND compatibility alias that happens to resolve to NVIDIA on the host.

Examples of vendor-specific libraries that are appropriate runtime candidates include:

```text
libGLX_nvidia.so.*
libEGL_nvidia.so.*
libGLES*_nvidia.so.*
libcuda.so.*
libnvcuvid.so.*
libnvidia-glcore.so.*
libnvidia-tls.so.*
libnvidia-*.so.*
vdpau/libvdpau_nvidia.so.*
```

Generic container-distribution aliases should remain owned by the container's package manager.

## Verified Unraid host matching

After translating the compat32 destination to `/usr/lib32` and filtering the conflicting generic GLX alias, the Omarchy container was inspected directly.

The following files were verified:

```text
/usr/lib32/libGLX_nvidia.so.595.99.02
/usr/lib32/libEGL_nvidia.so.595.99.02
```

Both were:

```text
ELF 32-bit LSB shared object, Intel i386
```

and both matched the active Unraid host driver:

```text
595.99.02
```

This is materially different from the earlier CachyOS failure where a 610-series host was paired with a 580-series Arch compat32 package.

The corrected Unraid experiment therefore does **not currently show evidence of that known NVIDIA branch mismatch**.

This does not yet mean every possible injected library and alias has been exhaustively validated. It means the important vendor libraries inspected so far are genuine host-matched ELF32 NVIDIA libraries.

## Native Steam package test on Unraid

With the corrected compat32 CDI mapping active, native Steam was installed experimentally using virtual Vulkan providers:

```bash
pacman -S --needed \
  --assume-installed 'vulkan-driver=1' \
  --assume-installed 'lib32-vulkan-driver=1' \
  steam
```

The initial transaction failed because `lib32-mesa` attempted to install:

```text
/usr/lib32/libGLX_indirect.so.0
```

while the CDI workaround had already mounted the host's alias at that path.

After filtering that one generic alias from CDI, the same transaction installed successfully.

The resulting ownership was:

```text
/usr/lib32/libGLX_indirect.so.0
    -> libGLX_mesa.so.0.0.0
    owned by lib32-mesa
```

while the actual NVIDIA vendor libraries remained host-matched `595.99.02` ELF32 files supplied by the runtime.

This is the expected ownership split for a native Arch graphics stack.

## Flatpak Proton on Unraid

Flatpak Proton testing on Unraid must be interpreted separately from native Steam testing.

During the native Steam experiments, Pacman installed generic native 32-bit packages including:

```text
lib32-libglvnd
lib32-mesa
lib32-vulkan-icd-loader
```

CachyOS testing independently demonstrated that adding this same class of outer-container native 32-bit graphics packages can break Flatpak Proton even when Flatpak's own NVIDIA GL and GL32 runtimes are correct.

Therefore a Flatpak Proton failure observed in an Unraid container **after native lib32 packages have been installed must not automatically be interpreted as evidence that the Unraid NVIDIA compat32 libraries are version-mismatched**.

The first Unraid Flatpak validation must use a clean Flatpak-focused image with no native 32-bit graphics stack.

### Required Unraid A/B validation

Test A:

```text
Omarchy container:
    normal 64-bit NVIDIA runtime injection
    corrected host-matched compat32 CDI enabled
    NO native lib32-libglvnd
    NO native lib32-mesa
    lib32-vulkan-icd-loader permitted
    NO distro lib32-nvidia-* package

Flatpak:
    matching NVIDIA GL/GL32 runtime

Test:
    launch the same known Proton game
```

If Test A passes, the CachyOS finding generalizes to Unraid and the prior Unraid Flatpak failure was caused by the added generic native `lib32` stack.

If Test A still fails, perform Test B:

```text
same clean Flatpak image
normal 64-bit NVIDIA runtime injection
compat32 CDI injection disabled
```

If Flatpak Proton works only in Test B, then even host-matched outer compat32 mounts are influencing Flatpak pressure-vessel and the Flatpak deployment should avoid compat32 CDI entirely.

Until those tests are complete, the Unraid native compat32 path and the Flatpak Proton path should be treated as separate validation tracks.

# Package ownership policy

The Docker image should have one clear owner for each class of graphics library.

## 64-bit NVIDIA vendor libraries

NVIDIA Container Toolkit owns the host-specific 64-bit NVIDIA userspace.

The base Dockerfile therefore protects the packages that would otherwise attempt to claim those runtime-injected paths:

```text
IgnorePkg = nvidia-utils egl-gbm egl-wayland2
```

This remains appropriate.

## Flatpak-focused 32-bit graphics

For a Flatpak-focused image:

```text
lib32-nvidia-utils
lib32-nvidia-580xx-utils
lib32-libglvnd
lib32-mesa
```

should not be installed merely for Steam.

`lib32-vulkan-icd-loader` may remain when another supported package requires
it. It remained installed in the successful Flatpak Proton A/B result.

Flatpak supplies the graphics runtime needed by Flatpak Steam and Proton.

There is no requirement to add `lib32-nvidia-utils` or `lib32-nvidia-580xx-utils` to `IgnorePkg` solely for this configuration because the image should not invoke a native installation path that requests those packages.

If a Flatpak-focused image still exposes Omarchy's native Steam installer, that installer should either be removed from the menu or clearly marked unsupported for that image rather than relying on `IgnorePkg` to block only part of its dependency transaction.

## Native Steam 32-bit graphics

A native Steam image has different requirements.

It needs generic 32-bit loader/runtime components and host-matched NVIDIA vendor libraries.

The package manager must not install an arbitrary distro NVIDIA driver package merely to satisfy:

```text
lib32-vulkan-driver
```

or similar virtual dependencies.

The preferred native architecture is to record that the NVIDIA vendor implementation is runtime-owned.

A container-only virtual package can provide:

```text
vulkan-driver
lib32-vulkan-driver
```

while the actual NVIDIA implementation comes from Container Toolkit / corrected compat32 CDI.

This prevents Pacman from choosing an unrelated `lib32-nvidia-*` branch.

If native Steam remains a supported menu action in a general-purpose image, the container-specific installer should be patched to understand this runtime ownership model.

`IgnorePkg` may be used as additional defense-in-depth, but it should not be the primary mechanism for resolving an explicitly requested `lib32-nvidia-*` dependency.

# Steam diagnostic images

The repository contains two thin diagnostic Dockerfiles used during the original NVIDIA investigation.

They are test instruments, not the intended final deployment architectures.

## `dockerfile.flatpak-steam`

This image installs system Flatpak Steam over the common Omarchy base.

Its purpose was to provide a control path where Steam uses Flatpak's own host-driver-matched NVIDIA GL/GL32 extensions.

The Flatpak control demonstrated that Proton could work without any usable native outer-container compat32 NVIDIA stack.

Conceptually:

```text
dockerfile.flatpak-steam
    |
    +-- diagnose Flatpak/runtime behavior
    +-- independent of native lib32-nvidia packaging
```

Build:

```bash
docker build -t local/omarchy-core:dev .

docker build -f dockerfile.flatpak-steam \
  --build-arg BASE_IMAGE=local/omarchy-core:dev \
  -t local/omarchy-flatpak-steam:test .
```

## `dockerfile.native-steam`

This image intentionally installs the repository's current native `lib32-nvidia-utils` while leaving the 64-bit NVIDIA driver userspace to Container Toolkit.

Its purpose was to expose and reproduce host/repository NVIDIA version mismatches.

It represents the original distro-lib32 diagnostic path and should not be confused with the newer host-injected compat32 design.

Conceptually:

```text
dockerfile.native-steam
    |
    +-- diagnose native host-vs-repository version mismatch
    +-- intentionally exposes mismatched driver branches
```

Build:

```bash
docker build -f dockerfile.native-steam \
  --build-arg BASE_IMAGE=local/omarchy-core:dev \
  -t local/omarchy-native-steam:test .
```

Every Compose template accepts `OMARCHY_IMAGE` for selecting a prebuilt test image.

Do not pass `--build` when selecting one of these test images, because doing so would rebuild the main Dockerfile over the selected tag.

Example:

```bash
OMARCHY_IMAGE=local/omarchy-flatpak-steam:test \
  docker compose --env-file .env -f compose.bridge.yml \
  up -d --no-build --force-recreate
```

Switch to the native diagnostic image:

```bash
OMARCHY_IMAGE=local/omarchy-native-steam:test \
  docker compose --env-file .env -f compose.bridge.yml \
  up -d --no-build --force-recreate
```

For native diagnostic testing, record:

```bash
nvidia-smi --query-gpu=driver_version --format=csv,noheader

pacman -Q | grep -E 'lib32-(nvidia|libglvnd|mesa)'

glxinfo32 -B
```

# Gaming image

`dockerfile.gaming` is the practical gaming derivative and should not silently combine incompatible NVIDIA graphics ownership models.

The current file implements the Flatpak-focused mode. It bakes system Flatpak
Steam into the image and retains UMU Launcher, Gamescope, 64-bit MangoHud, and
GameMode (including `lib32-gamemode`). It intentionally does not install native
Steam, `lib32-libglvnd`, or `lib32-mesa`.

`lib32-mangohud` is omitted because its dependency chain (`lib32-glew` to
`lib32-glu` to `lib32-libgl`) installs `lib32-libglvnd` and `lib32-mesa`, which
would recreate the confirmed Flatpak Proton regression. Flatpak games need a
Flatpak-compatible MangoHud Vulkan layer rather than the outer-container
32-bit package.

### MangoHud with Flatpak Steam

The image's native 64-bit `mangohud` package remains useful for compatible
outer-container applications, but it is not automatically visible inside the
Steam Flatpak sandbox. For Flatpak Steam games, install the Freedesktop Vulkan
layer matching Steam's runtime branch instead:

```bash
flatpak install --system flathub \
  org.freedesktop.Platform.VulkanLayer.MangoHud//25.08
```

The branch must match the Freedesktop Platform reported by:

```bash
flatpak info --show-runtime com.valvesoftware.Steam
```

Enable it for an individual Steam game with this launch option:

```text
MANGOHUD=1 %command%
```

To share the normal persistent MangoHud configuration with Flatpak Steam, run
as the `omarchy` desktop user:

```bash
flatpak override --user \
  --filesystem=xdg-config/MangoHud:ro \
  com.valvesoftware.Steam
```

Keep the runtime branch synchronized after Steam moves to a newer Freedesktop
Platform. The optional Vulkan layer is installed separately for each branch;
`flatpak update` does not replace an older branch with the new one. See the
[Flathub MangoHud extension](https://github.com/flathub/org.freedesktop.Platform.VulkanLayer.MangoHud)
for current configuration guidance.

The validated CachyOS result establishes that a Flatpak-focused gaming build should not add a native generic 32-bit graphics stack merely to support Flatpak Steam.

For NVIDIA, the gaming image should therefore explicitly define which Steam mode it targets.

## Flatpak-focused gaming mode

Preferred portable configuration:

```text
Steam:
    Flatpak

Outer container:
    normal 64-bit graphics stack
    no Steam-specific native lib32 graphics stack

NVIDIA:
    host 64-bit runtime for Omarchy desktop
    Flatpak host-matched GL/GL32 extensions for Steam/Proton
```

This configuration has been validated on CachyOS for both native Linux Steam games and Proton games.

It remains to be repeated in a clean Unraid container.

## Native gaming mode

Native Steam/UMU requires:

```text
generic native 32-bit loader/runtime components
+
host-matched NVIDIA compat32 vendor libraries
```

The NVIDIA vendor libraries must come from a corrected Container Toolkit/CDI path rather than from a fixed driver package baked into the image.

A native gaming image may use a virtual package providing:

```text
vulkan-driver
lib32-vulkan-driver
```

to prevent Pacman from selecting an unrelated NVIDIA branch.

Runtime compat32 injection must provide NVIDIA vendor libraries only. Generic distribution-controlled GL/GLVND aliases such as:

```text
libGLX_indirect.so.0
```

must remain owned by the container distribution.

## Combined native + Flatpak gaming image

Supporting both native Steam and Flatpak Proton in one NVIDIA image is possible only if the native 32-bit environment does not interfere with Flatpak pressure-vessel.

Current testing demonstrates that simply adding:

```text
lib32-libglvnd
lib32-mesa
```

to an otherwise working Flatpak image can cause Proton/DXVK failure. The
standalone `lib32-vulkan-icd-loader` remained installed when functionality was
restored, so current evidence does not implicate it by itself.

Therefore a combined image must not be considered validated merely because:

* native Steam opens,
* Flatpak Steam opens,
* or native Linux games launch.

The same Proton workload must be tested through both runtime models.

If pressure-vessel isolation cannot be made reliable with the native `/usr/lib32` stack present, the durable architecture is to keep Flatpak-focused and native-gaming variants separate.

# Flatpak runtime inspection

When diagnosing Flatpak Steam, record the application and NVIDIA runtime state:

```bash
flatpak --user info com.valvesoftware.Steam

flatpak --user list --runtime | \
  grep -Ei 'nvidia|GL32|Platform.GL' || true

flatpak --system list --runtime | \
  grep -Ei 'nvidia|GL32|Platform.GL' || true

nvidia-smi --query-gpu=driver_version --format=csv,noheader
```

Do not infer a Flatpak NVIDIA mismatch solely from the existence of outer-container `lib32` libraries.

First compare the Flatpak Steam, Freedesktop Platform, NVIDIA GL, and NVIDIA GL32 versions between the working and failing images.

The CachyOS regression demonstrated that these Flatpak components can remain identical while a change to the outer container's native `/usr/lib32` stack alone breaks Proton.

# Native compat32 inspection

For native Steam/UMU testing, inspect the actual outer-container libraries:

```bash
nvidia-smi --query-gpu=driver_version --format=csv,noheader

pacman -Q | grep -E \
  'lib32-(nvidia|libglvnd|mesa|vulkan)'

file /usr/lib32/libGLX_nvidia.so.* 2>/dev/null
file /usr/lib32/libEGL_nvidia.so.* 2>/dev/null
```

On a host-injected compat32 configuration, the NVIDIA vendor filenames should match the active host driver version.

For example, on the tested Unraid `595.99.02` host:

```text
/usr/lib32/libGLX_nvidia.so.595.99.02
/usr/lib32/libEGL_nvidia.so.595.99.02
```

were verified as ELF32 NVIDIA libraries.

Also check ownership of generic aliases:

```bash
file /usr/lib32/libGLX_indirect.so.0

pacman -Qo /usr/lib32/libGLX_indirect.so.0
```

On Arch with `lib32-mesa` installed, the expected result is:

```text
/usr/lib32/libGLX_indirect.so.0
    -> libGLX_mesa.so.0.0.0

owned by lib32-mesa
```

A CDI mount occupying that path indicates that the runtime is injecting too broad a set of host compatibility aliases.

# Validation matrix

Use the following matrix when changing Steam packaging, NVIDIA runtime behavior, or the gaming image.

| Host                    | Steam path | Outer native lib32 graphics | NVIDIA compat32 source                         | Expected / status                                               |
| ----------------------- | ---------- | --------------------------: | ---------------------------------------------- | --------------------------------------------------------------- |
| CachyOS / general Linux | Flatpak    |                          No | Flatpak GL32                                   | Proton validated                                                |
| CachyOS / general Linux | Flatpak    |                         Yes | Flatpak GL32 + outer generic lib32             | Proton regression reproduced                                    |
| CachyOS / general Linux | Native     |                         Yes | mismatched distro 580xx                        | Failed                                                          |
| CachyOS / general Linux | Native     |                         Yes | host-matched 610.57.04                         | 32-bit GLX/native Steam validated                               |
| Unraid                  | Flatpak    |                          No | Flatpak GL32; compat32 CDI may also be present | Pending clean validation                                        |
| Unraid                  | Flatpak    |                         Yes | Flatpak GL32 + native generic lib32            | Failure observed; not valid evidence of NVIDIA version mismatch |
| Unraid                  | Native     |                         Yes | corrected host 595.99.02 CDI                   | Package/library path validated; Proton validation pending       |

Whenever a regression appears, first determine which row changed before modifying the compositor, Sunshine, or host driver.

# Current conclusions

The current NVIDIA/Steam findings are:

1. NVIDIA Container Toolkit's released JIT-CDI path can omit the host's required 32-bit NVIDIA libraries.

2. Installing a random Arch `lib32-nvidia-*` package is not a portable substitute because it may belong to a different NVIDIA branch than the host.

3. Flatpak Steam does not require a native outer-container compat32 NVIDIA stack. Its matching NVIDIA GL/GL32 extensions can support Proton independently.

4. On CachyOS, adding a generic native `/usr/lib32` graphics stack to the outer Omarchy container broke an otherwise unchanged Flatpak Proton configuration.

5. Removing that native outer `lib32` graphics stack restored Flatpak Proton on CachyOS.

6. The Unraid compat32 experiment successfully discovered host `595.99.02` ELF32 NVIDIA vendor libraries, but required translating their Arch container destination to `/usr/lib32`.

7. The Unraid compat32 experiment must not inject generic host aliases such as `libGLX_indirect.so.0` when the container distribution owns those paths.

8. A Flatpak failure in a container that has subsequently installed `lib32-mesa`, `lib32-libglvnd`, or related packages is not by itself evidence that the host-injected NVIDIA compat32 libraries are mismatched.

9. Flatpak-focused and native Steam configurations have different graphics ownership requirements and must be validated separately.

10. If one image cannot cleanly support both ownership models, separate Flatpak-focused and native-gaming variants are preferable to baking a fixed NVIDIA driver into the image.

# Image release policy

Monthly Omarchy releases should produce a new image from `main`.

Live package/security updates can be installed in a disposable container for validation, but persistent system package changes should be incorporated into the next Dockerfile rebuild rather than treated as durable container state.

The `main` workflow publishes the supported image tags to GHCR.

Before promoting an NVIDIA gaming image, validate at minimum:

```text
Omarchy desktop
Sunshine NVENC
Moonlight video/audio/input
Flatpak Steam native Linux game
Flatpak Steam Proton game
```

If native Steam is part of the image's supported contract, also validate:

```text
host driver version
ELF32 NVIDIA vendor-library version
32-bit GLX
native Steam UI
native Proton Vulkan/DXVK
```

Do not consider native Steam validated solely because the Steam client UI opens.
