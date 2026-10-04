# Linux maintainer notes

This is a native x86_64 Vulkan port of Pinyon Shift 0.4.0. Original authorship,
branding and BSD licensing are preserved. The source export contains the
runtime, Linux launcher and build tools; developer tests and local data stay
in the development checkout.

## Build and use

Install Git, Python 3.11.8+, jq, a C++ build environment and SDL development
libraries. Run `./tools/build-launcher.sh --download`, then
`./tools/launch-launcher.sh`. Select your own Forza Horizon ISO and its disc
edition. Setup verifies exact hashes before extraction and compilation.
Only the catalogued retail and experimental Polish NXE images are supported;
this is not a launcher for other Forza games or arbitrary regional revisions.

The source directory can live anywhere. Setup remembers the absolute ISO path
in `.local/setup-state.json`; the ready screen displays it. If no path was
recorded, the launcher offers the only ISO in `.local/images`, when unambiguous.
A missing or moved image can be replaced using **Choose another ISO**. Changing
edition requires verification and rebuilding. Language selection is independent.
Saves and caches live in `.local/preview`, or the explicit `--state-root`.

## Integration map

- `launcher/PinyonShift.Launcher.Linux`: Avalonia UI and Linux process adapter.
  Commands use argument lists, not shell interpolation. The UI retains only a
  bounded log tail; full output is written to `.local/logs`. While playing,
  log refresh runs once a second and avoids redundant process enumeration.
- `tools/setup-preview.sh`: verify, provision, extract and build. Existing
  extraction trees are preserved when replacing an edition; saves are separate.
- `tools/linux_support.py`: verified disc catalog and backed-up settings edits.
  The launcher render limit uses the existing FH1 render cvar, preserving the
  presentation limit and simulation settings. Try 60 fps at 1×; it is optional,
  and no default is silently changed. Performance gains have not been measured
  for these launcher changes.
- `tools/prepare_fh1_vulkan.py`: existing first-start shader preparation in a
  private profile. Keep persistent caches; the opening route cannot precompile
  every effect. Its route file is a runtime dependency of preparation, even in
  the otherwise test-free export.
- `config/linux-port.json`, `patches/linux/shiftglue-linux.patch` and
  `tools/apply-linux-sdk.py`: pinned upstream SDK and reproducible patch.
  Keep these together. For upstream adoption, integrate the patch into ShiftGlue
  and update the pin before retiring the patch.
- `src/save/live_profile_linux.cpp`, `src/crash_reporter_posix.cpp` and Linux
  branches in shared sources: native memory inspection and fault handling.
  Preserve the SDK's handling of recoverable guest faults.

The Linux scripts and launcher are separate from the original Windows workflow.
Shared runtime sources retain platform guards. No game files, generated guest
code, personal paths, saves or compiled game binary belong in a GitHub upload.
Upload the exported `source/` contents as the repository root. The launcher
archive is a separate release asset. Set `config/linux-publication.json` to
its destination fork; it controls credits and the release selector.

This pass adds no tests and runs no test suites. Compilation establishes build
compatibility only; controller/audio behavior, other distributions and gameplay
frame times still need interactive qualification.

## Language repair and PC controls

The trainer's wristband choices previously stored bare string values, including
`cheat_set_profile_fields =` with no value. The SDK rejects the entire TOML file,
so language selection could be saved correctly yet the game started in English.
`src/ui/settings_menu.cpp` now quotes both empty and nonempty string choices.
Before Linux launch, `prepare_launch_config` repairs only those known legacy
forms, backs up the previous configuration, and validates TOML. Unrelated syntax
errors are reported instead of launching with silently discarded settings.

The launcher's Driving controls selector explicitly applies `--controls` through
`linux_support.py`. Keep current controls does not overwrite custom bindings.
The PC presets use existing SDK mouse/controller emulation: W/S throttle/brake,
A/D steering, Space handbrake, R rewind, Q/E downshift/upshift, Esc pause, Tab
back/map, and mouse camera or mouse steering. Actions assume the game's default
controller layout. These remain emulated controller inputs, including console
button prompts; this does not add the modern Forza PC input engine. F6 opens
host settings; the existing guest-input capture disables mouse lock there.
Settings backups also preserve any replaced bindings.

## Temporal upscaling feasibility

FSR 1 remains the implemented Linux output scaler. The SDK's optional Vulkan
`DispatchTemporalUpscaler` currently passes the color image as both depth and
motion vectors, with zero jitter. That is placeholder input, not a usable FH1
FSR 2/3 implementation, and the FidelityFX build option remains off.

Newer temporal FSR and DLSS require renderer integration with valid depth,
motion vectors, projection jitter, history resets and correct HUD composition.
Replacing Windows DLLs cannot add those inputs to a native Linux executable.
Before enabling these modes, expose the actual guest buffers with matching
frame lifetimes, then integrate an appropriate native Vulkan SDK and assess
image stability in motion. No FSR 3/4, DLSS or frame-generation support is claimed
by this release.

References: [AMD FSR upscaler inputs](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/),
[current AMD FSR SDK](https://gpuopen.com/manuals/fsr_sdk/),
[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS).

## Version 1.0 baseline

Port version 1.0.0 is the first playable release confirmed by the maintainer,
based on upstream game/runtime sources 0.4.0. The PC preset now maps arrow
keys to D-pad navigation and Shift+Arrows to the camera. Linux launch migrates
only matching old PC default bindings, preserving custom layouts and backups.
No FSR/DLSS experimentation belongs in the v1.0.0 release snapshot. Future
renderer work should use a separate branch based on that tag.
