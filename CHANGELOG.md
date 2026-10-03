# Changelog

## 0.4.0 - 2026-10-03

- Fixed Vulkan losing the GPU on AMD Radeon cards: the game could wait on a
  fence before it was submitted. Fixed three other Vulkan validation errors.
- Fixed green, white and pink blocks while shaders compile, a stop at guest
  address 0x38 with the Treasure Map, a setup compile error on some PCs, and
  a hang or crash when the render job queue overran during heavy frames.
- Played the start-line crowd and the car purchase cameras at console speed
  at 60 and 120 fps.
- Showed late frames as soon as they are ready, steadying 120 fps at 1x.
- Made 2x to 4x lighter on the GPU (compute texture loads, resolves written
  into the textures that read them) and the GPU recorder thread cheaper.
- Prepared Vulkan shaders during setup.
- Added Build Android APK to the launcher (alpha): the game cross-compiled
  for arm64 Android from the PC build, with touch controls and presets.
- Wrote the guest threads' stacks to the log when the game stops responding.

## 0.3.1 - 2026-10-01

- Fixed setup and launches with Direct3D 12 stopping while preparing graphics
  because the shader sources were looked up in a developer checkout's path.

## 0.3.0 - 2026-10-01

- Rendered the game with a native renderer for *Forza Horizon* on Vulkan (the
  default) or Direct3D 12, with shader packs prepared and repaired locally.
- Rendered at 60 or 120 fps with the simulation at the right speed, and at 1x
  to 4x internal resolution, changeable in game, with FSR 1 or CAS output
  scaling.
- Added in-game settings (F6 or the pause menu) for display, graphics, audio
  and controls, most applying at once, with Performance 120 and Quality 60
  presets.
- Added ultrawide Hor+ with a 16:9 HUD and a field of view setting, monitor,
  window size, aspect and frame-rate settings.
- Added a gamertag and picture, an achievements list, PNG photos, save
  backups, the disc's 18 languages, button remapping and mouse camera or
  steering.
- Added a trainer (credits, game speed, time of day, free camera, collectibles
  on the map) on a separate modded profile, and mods: native plugins, file,
  archive, database and texture replacement, text, HUD labels and menu
  actions.
- Included the Treasure Map add-on, on by default with a launcher toggle.
- Redesigned the launcher, added a Settings panel that shows the rendered and
  output resolutions, and portable installs.
- Made setup report the failed step and its first real error, retry locked
  runtime copies, start PowerShell by its full path, explain a declined
  administrator prompt, tolerate shader-pack misses on slower PCs, unpack the
  compiler with Windows' own tar and size the build's parallel jobs to memory.

## 0.1.1 - 2026-08-27

- Fixed launcher-package builds that completed translation but failed while
  probing absent Git metadata.
- Removed the packaged setup workflow's Python dependency by verifying codegen
  warnings with Windows PowerShell.

## 0.1.0 - 2026-08-26

- First Windows public playable preview.
- Updated the pinned ReXGlue SDK from 0.9.0 to 0.10.0, including its threading,
  audio, input, GPU, diagnostics, and incremental-codegen improvements, while
  preserving the project's runtime compatibility patch set.
- Added a graphical launcher that verifies a supported disc and performs the
  complete local toolchain, extraction, generation, and build workflow.
- Added reproducible dependency pins and a public-source repository boundary.
- Added resilient ReXGlue/submodule download retries, disabled SDL's optional
  libusb probe on Windows, and made Xbox menu acceptance accessible through
  Space or left click with an in-launcher control hint.
- Made launcher setup ignore quoted `PATH` entry syntax when initializing the
  Microsoft build environment and always use the verified pinned MinGit.
- Restored motion blur around the player car and removed the stale gameplay
  limitation list after the latest compatibility fixes.
- Added launcher controls for validated graphics experiments, including 2x
  resolution scaling, anisotropic filtering, and post-effect selection.
- Fixed launcher-package builds that completed translation but failed while
  probing absent Git metadata, and removed Python from warning verification.
- Added an experimental 3x internal-resolution preset for 4K-class output.
