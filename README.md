# Pinyon Shift

Pinyon Shift is a Windows-only playable preview of a native recompilation of
the Xbox 360 release of *Forza Horizon*. The project is early, imperfect, and
surprisingly drivable.

This repository contains the launcher, build tools, host code, configuration,
and pinned ShiftGlue submodule needed to create the preview on your own computer. It does **not**
contain the game, game assets, generated translations, or a prebuilt game
executable.

> **Highly experimental renderer preview.** The source now renders only with the
> FH1 native renderer. Rendering regressions, accelerated NPC animations, and
> severe slowdowns in some areas remain possible. See the latest
> [preview release notes](docs/releases/0.1.2-preview.3.md) for what the last
> published build contains.

## Renderer status

The FH1 native renderer is the only renderer. It executes every draw, clear,
resolve and swap the game issues, in order, with the game's original shaders
from an offline shader pack produced on your machine; the Xenos-emulating
D3D12 renderer has been removed. Internal resolution scales 1x to 4x are
supported; any other scale fails graphics setup. SETTINGS > GRAPHICS >
RESOLUTION SCALE changes the scale in game when that scale's shader pack is
already prepared (PREPARE ALL SCALES prepares them all), and at the next start
otherwise. If the game uses a shader the
pack lacks, the draw is skipped and the shader is recorded, and the next launch
prepares the pack again to include it. Lower hardware requirements and AMD and
Intel GPUs remain unqualified.

See [development findings and priorities](docs/DEVELOPMENT.md) for measured
results, known regressions, the documentation map and remaining work.

## Play

1. Download `PinyonShift-Launcher.zip` from the latest release.
2. Extract the two files to a folder and run `PinyonShift.Launcher.exe`.
3. Select an ISO you personally dumped from a supported original disc.
4. Confirm ownership, then choose **Verify & Build**.
5. Leave the launcher open while it installs the Windows build tools and builds
   the preview. The first build can take 20–60 minutes and requires roughly
   25 GB of free disk space.

The preview launcher is not code-signed yet, so Windows may identify it as an
unrecognized app. Use only the archive attached to this repository's release
and verify its published SHA-256.

The launcher verifies the image before reading it. Unsupported or modified
images are rejected. Your image and extracted game files stay on your machine.
The launcher downloads build tools and the pinned ReXGlue source, extracts the
disc locally, generates the translation locally, and compiles the executable
locally. Administrator permission is requested only if Visual Studio Build
Tools must be installed.

To build on another drive, use **Choose folder** under **Local build** in the
packaged launcher. The launcher remembers your choice for subsequent launches.
This selects an installation; it does not move an existing installation or save.
You can also override the remembered location from PowerShell:

```powershell
$env:PINYON_SHIFT_INSTALL_ROOT = 'D:\Games\PinyonShift'
.\PinyonShiftLauncher.exe
```

Source, downloaded tools, extracted game data and the default save/cache tree
will live beneath that folder. Existing installations and saves are not moved;
an existing `PINYON_SHIFT_STATE_ROOT` override still takes precedence for saves
and caches. Launchers inside a repository checkout continue to use that checkout.
This is a custom build location, not a portable binary distribution: Microsoft
Build Tools still need system-drive space, and generated CMake paths are tied to
the build location.

If setup fails, `.local/logs/setup-error.json` now includes the failed command's
exit code, build-log path and last 80 output lines. The complete configure/build
logs are in the same folder. Include the first actual compiler or CMake error
when reporting a failure; the final "build failed" line alone cannot identify it.

Supported today: the USA retail base disc, serial `MS-2505`, title ID
`4D5309C9`. Windows 10/11 x64 and a DirectX 12-capable GPU are required.
The launcher includes 2× and experimental 3× (4K-class) internal-resolution
scaling for capable GPUs; other scales are not supported.

This is a public preview, not a finished remaster. Please report reproducible
problems using the issue template and do not attach game files or generated
code.

## Reporting crashes and bugs

Keep the launcher open while playing. If the game exits unexpectedly, the
launcher catches the exit, creates a sanitized diagnostic ZIP, and offers one
button to open a prefilled GitHub issue with that ZIP selected in Explorer.
Attach the selected ZIP and add the shortest reliable reproduction steps.

The public report includes build hashes, a stable crash ID, exception details,
the end of the runtime log, runtime settings, Windows build, CPU, GPU, and driver
versions. It excludes the game, saves, generated code, input capture, local
paths, and memory dumps. A fuller dump stays on the player's computer and should
only be shared privately if a maintainer requests it. Non-crash bugs can be
reported with **Report a problem** in the launcher.

## Build from source

From a PowerShell terminal in a repository checkout:

```powershell
.\tools\setup-preview.ps1 -IsoPath C:\path\to\your-disc.iso
.\tools\launch-preview.ps1
```

The setup script provisions pinned dependencies, initializes ShiftGlue,
verifies/extracts the disc, generates translated source, and
builds Release. See [Building](docs/BUILDING.md) and
[Troubleshooting](docs/TROUBLESHOOTING.md) for details.

## Roadmap

Longer-term direction, in no particular order. The current preview supports
none of it. The ordered plan, with vertical slices, sizes, dependencies and
acceptance gates, is the [native port backlog](docs/NATIVE_PORT_BACKLOG.md).

- [ ] Lower the hardware requirements and qualify AMD and Intel GPUs
- [ ] Fix the remaining rendering regressions
- [ ] Make the first build faster and fully validated
- [ ] Support more disc regions and languages
- [ ] Change resolution and render scale while the game is running
- [ ] Apply graphics settings without restarting the preview
- [ ] Support ultrawide (21:9 and wider) displays
- [ ] Add controller remapping
- [ ] Ship the UI extension API
- [ ] Ship a modding API for loading custom content
- [ ] Build for macOS and Linux
- [ ] Port the runtime to Android
- [ ] Ship a Steam Deck build
- [ ] Support portable installs
- [ ] Sign the launcher and preview executables
- [ ] Import cars from *Forza Horizon 2*

Measured findings and validation rules are in
[development findings and priorities](docs/DEVELOPMENT.md).

## Project boundaries

Only independently authored project files are licensed under the
[BSD 3-Clause License](LICENSE). Microsoft, Xbox, Turn 10 Studios, Playground
Games, *Forza Horizon*, and third-party dependencies remain the property of
their respective owners. Pinyon Shift is not affiliated with or endorsed by
them. See [Legal and distribution](docs/LEGAL.md) and
[Third-party notices](THIRD_PARTY_NOTICES.md).

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). Repository checks reject disc
images, executables, generated translations, extracted assets, build products,
and other machine-local material.
