# Native port backlog

Status: **open; created 2026-09-28** at `dev` checkpoint `02ceca9` (ShiftGlue
`aff6202`). This is the working plan that follows the closed
[Xenos retirement backlog](native-renderer/XENOS_RETIREMENT_BACKLOG.md). It
replaces the unordered roadmap list in the README as the source of truth for
what comes next. Assessment evidence for every claim below was gathered on
2026-09-28 from the current tree; file references use repository-relative
paths, with `sdk/` standing for `thirdparty/shiftglue-sdk/`. Raw research
notes are kept locally under `.local/backlog-research/` and are not
distributed.

## Goal

Make Pinyon Shift feel like a native PC release of *Forza Horizon*, not a
renderer preview: every setting in the game, any display, correct behaviour at
any frame rate, a real profile and achievements, a mod host, a trainer for
playthroughs, and a renderer and CPU path that use a modern machine instead of
emulating a 2012 console. Ports to Linux, macOS and Android follow on the same
architecture.

"Native PC game" is done when all of the following hold:

1. **No launcher after install.** Every player setting is changed from the
   pause menu, applies without a restart where the engine allows it, and is
   labelled "restart required" where it does not. The launcher installs,
   verifies, builds and reports crashes.
2. **No ImGui for players.** Every player-facing surface (settings, profile,
   achievements, system dialogs, toasts, trainer) is drawn by a host UI layer
   styled from the game's own fonts, textures and layout. ImGui remains for
   developer overlays only (F3, F4, console).
3. **Any display.** Any window size and aspect ratio, borderless fullscreen on
   any monitor, integer internal scales up to 4x with a quality downscale,
   Hor+ ultrawide with a correct FOV and anchored HUD, frame caps and VRR.
4. **Correct at any frame rate.** Unlocked frame rate stays the default and
   no animation, NPC, UI transition or audio path runs at the wrong speed.
5. **Fast.** The race window is GPU-bound rather than CPU-bound on the
   baseline machine, guest threads are placed and prioritised on modern CPUs,
   and no core is burned spinning.
6. **Native profile.** A chosen gamertag and picture, achievements shown and
   unlocked in-game, language selectable from the disc's 18 languages, saves
   backed up and restorable in-game.
7. **Mods and cheats.** A versioned plugin ABI, an asset overlay, data patches
   and a UI extension API, with documentation and sample mods; a trainer that
   never touches an unmodded save.
8. **Portable.** The same executor core drives D3D12 and Vulkan; Linux and
   Steam Deck ship first, macOS through MoltenVK second, Android third.

## How this backlog is organised

- **Vertical slices.** Each slice `NP-n` ends with something a player or
  modder can use, and cuts through guest hooks, SDK, host UI, config, tools,
  tests and docs as needed. Items inside a slice are `NP-n.m`.
- **Sizes** are for one engineer: S ≤ 1 week, M 1–3 weeks, L 1–2 months,
  XL > 2 months. Sizes are estimates from the code, not commitments.
- **Gates** reuse the validation rules in
  [development findings](DEVELOPMENT.md#validation-and-evidence): same seed,
  route and settings for control and candidate, three runs per arm, golden
  frame replays (`tools/test-fh1-frame-replays.py`), the affected render-test
  routes, pose-drift and simulation-time gates, and save payload hash equality
  where guest timing or numerics change. Never touch the AppData save; use
  seeds from `tools/create-render-seed.py` ([AGENTS.md](../AGENTS.md)).
- **Portability guardrails apply from NP-1 on.** New host code uses the SDK
  platform layer (no `Windows.h` outside diagnostics), fixed-function shaders
  are authored in HLSL and compiled to both DXBC and SPIR-V, and the shader
  pack format becomes backend-neutral in NP-9 before the Vulkan executor in
  NP-12 needs it.
- **Release trains.** NP-0 to NP-3 target `0.3.0`; NP-4 to NP-8 target
  `0.4.0`; NP-9 to NP-11 target `1.0` (Windows complete); NP-12 to NP-14 are
  the `1.x` platform releases. Trains can be re-cut; the dependency graph in
  the slice map is what matters.

## Where the code stands

| Area | Finding | Evidence |
| --- | --- | --- |
| Renderer identity | What shipped is the Xenia D3D12 backend with a native surface owner. The EDRAM render-target cache and runtime shader translation are gone, but per-draw register-to-pipeline derivation, Xenia's constant-buffer and root-signature model, the tiled guest-memory texture cache and a runtime DXBC geometry-shader emitter are still on the hot path. The native executor replaces one component (surfaces, tiles, clears, resolves) inside `IssueDraw`. | `sdk/src/graphics/d3d12/command_processor.cpp:2303-2846`, `sdk/src/graphics/d3d12/pipeline_cache.cpp:1695-1916, 2578-3622`, `sdk/src/graphics/d3d12/texture_cache.cpp:1662-2151` |
| Dead code | The Vulkan backend, SPIR-V translator, EDRAM cache and PM4 disassembler (about 40k lines) are not compiled on Windows. Two shader-hash allowlists remain on the hot path. Modern ZPD paths, dead readback cvars and SNR-M02 trace cvars are compiled but idle. 73 of 174 tool scripts belong to retired scene-native research and 40 of them require a log authority no build can produce. | `sdk/src/graphics/CMakeLists.txt:45-101`, `command_processor.cpp:2360-2372, 2480-2495`, `command_processor.cpp:41-95, 254-396` |
| Renderer performance | Race window: 24.95 ms median frame, 12.61 ms GPU span, so the frame is CPU-bound on the single GPU Commands thread. The whole-frame deferred tape is replayed serially at swap, `OMSetRenderTargets` is recorded per draw, fetch constants re-upload on any write, and every resolve writes the guest tiled layout into the memory mirror and is untiled again on the next fetch (15.3 M of 53 M fetches are resolve-sourced, front buffer included). Depth transfers take 9 passes because stencil is almost never proven zero. | [baselines](native-renderer/NATIVE_PERFORMANCE_BASELINES.md), `command_processor.cpp:3281`, `fh1_native_executor.cpp:1816, 1901-1966, 1053-1082`, `command_processor.cpp:1778-1780, 3845-3856` |
| CPU and threading | Guest threads are 1:1 host threads; priorities and affinities are ignored by default and nothing is pinned. The title spends two thirds of its time busy-polling a word the GPU thread writes. Texture write-watches cost a syscall plus a TLB shootdown per 4 KiB page. Every event or wait takes one process-wide recursive mutex. The build is pinned to SSE4.1, so 11,725 fused multiply-add sites call a library function. The timer queue spin-waits on a core. | `sdk/src/system/xthread.cpp:42-46, 1025-1073`, `sdk/src/system/xmemory.cpp:2112-2293`, `sdk/src/system/xobject.cpp:370-449`, `cmake/PinyonShiftRexGlue.cmake:56-66`, `sdk/src/core/timer_queue.cpp:145-147` |
| Frame rate | Unlocked frame rate is the shipped default: guest vblank runs at twice the detected refresh, gameplay integrates a variable delta observed at `0x823EDB84`, and the source-60 and HFR routes gate distinct presents and simulation time. Accelerated NPC and title animations remain an open regression with no address located. | `sdk/src/graphics/graphics_system.cpp:170-227`, `src/pinyon_shift_app.cpp:57-86`, [release notes](releases/0.1.2-preview.3.md) |
| Display | The guest still renders 1280×720 times an integer scale of 1–3 because the DXBC translator bakes the scale into shader immediates and each pack is keyed by scale. Output is letterboxed to the guest video-mode aspect; a 21:9 guest mode would stretch. No projection, FOV, safe-area or back-buffer hook exists. Borderless fullscreen only, no exclusive mode, no HDR, presenter downscale is bilinear (FidelityFX is off by default). | `fh1_native_executor.cpp:407-413, 1952`, `sdk/src/graphics/pipeline/shader/dxbc_translator.cpp:664-747`, `sdk/src/ui/presenter.cpp:874-1010`, `sdk/CMakeLists.txt:23` |
| Player-facing UI | Everything the player sees from the host is ImGui with a 10 px debug font and a green theme: XAM message box, virtual keyboard, disc error, achievement toast, F7 list. Achievements, gamercard, friends, marketplace and sign-in system screens are stubs. The gamertag is fixed to "User", `XGetLanguage` is hard-coded English, button prompts are always Xbox glyphs, and no controller remapping exists. | `sdk/src/ui/imgui_drawer.cpp:30-87`, `sdk/src/kernel/xam/xam_ui.cpp:211-663`, `sdk/src/system/xam/user_profile.cpp:28-29`, `sdk/src/kernel/xam/xam_info.cpp:185-198` |
| Game UI and assets | The game's UI is Anark Gameface (`AnarkBGF` scenes under `Scenes/ui4` in `media/UI.zip`, with Lua scripts) plus LSB2 string tables. Same-length label rewrite and row hiding are proven; adding a row is blocked at scaler-binding registration. Archives are PKZIP with Xbox LZX entries and stored entries; only a decompressor exists. Car, upgrade, wristband, event and time-of-day data is a plain SQLite database, `media/db/gamedb.slt`. | [UI API plan](UI_API_PLAN.md), `tools/fh1-ui-scene-insert.py`, `tools/fh1_archive_extract.cpp:16-57`, `.local/game/base/media/db/gamedb.slt` |
| Modding foundations | About 90 mid-asm hooks in `config/rexglue/analysis/*.toml` are the interception surface; there are no whole-function overrides and no plugin loader. Runtime DLLs can only replace indirect calls through the dispatch table. The VFS has no overlay device, but the host can swap the game mount before launch. Saves are raw files under a fixed profile identity; the plaintext save body is visible at `0x82C666D0` before encryption and is already edited there. | `sdk/include/rex/hook.h`, `sdk/include/rex/system/function_dispatcher.h:85-113`, `sdk/src/system/runtime.cpp:294-375`, `src/pinyon_shift_runtime_hooks.cpp:492-530` |
| Portability | The SDK already has Linux, macOS and ARM64 platform layers, SDL3 window/input/audio, a pinned MoltenVK stack and simde-based NEON for the PPC headers; the generated code is Clang-dialect with zero SEH scopes. The host project is Windows-only by construction (WPF launcher, PowerShell tools, `Windows.h` in diagnostics and app, D3D12-only executor, shader pack and texture-cache additions). The Vulkan backend has drifted about 25 base-class commits and its compile state is unverified. | `sdk/CMakePresets.json`, `sdk/src/core/CMakeLists.txt`, `src/pinyon_shift_diagnostics.cpp:3-5`, `sdk/src/graphics/vulkan/` |
| Distribution | Releases ship only independently authored source and the launcher; the user builds the executable and shader packs from their own disc. First build is 20–60 minutes and about 25 GB. CI checks the boundary, Python tools and the launcher; it never compiles C++ and the four C++ test targets are excluded from the default build. | [legal](LEGAL.md), `.github/workflows/ci.yml`, `CMakeLists.txt:47-79` |

## Slice map

| ID | Slice | Player- or modder-visible outcome | Size | Depends on | Train |
| --- | --- | --- | --- | --- | --- |
| NP-0 | Clean native baseline | Smaller renderer DLL, no allowlists, one occlusion path, car-selection textures fixed, stale tools gone, graphics prepared only when graphics code changes | M | — | 0.3.0 |
| NP-1 | In-game settings and host UI layer | "SETTINGS" in the pause menu opens a native-looking screen; hot settings apply instantly | L | NP-0 | 0.3.0 |
| NP-2 | Fast frame, pass 1 | Measurably shorter race frames from the renderer's CPU path | M | NP-0 | 0.3.0 |
| NP-3 | Modern CPU, pass 1 | Threads placed and prioritised, no spinning cores, NPC and UI animations at real time | M–L | NP-2.0 | 0.3.0 |
| NP-4 | Any display | Any window size, 4x scale, sharp downscale, Hor+ ultrawide with FOV slider, frame caps and VRR | M–L | NP-1 | 0.4.0 |
| NP-5 | Native profile, achievements, dialogs, language | Gamertag and picture, in-game achievements, styled system dialogs, 18 languages, save backups, photo export | M | NP-1 | 0.4.0 |
| NP-6 | Input | Controller remapping, keyboard prompt text, haptics options | S–M | NP-1 | 0.4.0 |
| NP-7 | Mod host v1 | Plugin ABI, hook points, symbol table, asset overlay, isolated modded profile, docs and samples | M–L | NP-1 | 0.4.0 |
| NP-8 | Cheat menu v1 | Trainer screen: time scale, teleport, career skip, save editor, world toggles | M | NP-7.1, NP-7.5 | 0.4.0 |
| NP-9 | Fast frame, pass 2 | GPU-bound race window; executor core split; backend-neutral pack | L | NP-2, NP-3 | 1.0 |
| NP-10 | Content mods | Per-asset overrides, database patches, texture replacement, optional Lua | M–L | NP-7 | 1.0 |
| NP-11 | UI extension API, production | Extensions add and drive menu items and HUD widgets through the stable API | M (+L research) | NP-1, NP-7 | 1.0 |
| NP-12 | Linux and Steam Deck | Native Linux build with the Vulkan executor and Deck qualification | XL | NP-9 | 1.x |
| NP-13 | macOS | Apple Silicon build through MoltenVK | L | NP-12 | 1.x |
| NP-14 | Android | ARM64 Vulkan build with a cross-build workflow | XL | NP-13 | 1.x |
| NP-X | Quality and tooling | C++ tests and SDK build in CI, pruned tools, hardware qualification | ongoing | — | all |
| NP-D | Distribution and first run | Faster first build, launcher core reusable across platforms, signing | ongoing | — | all |

## Working order

**Current goal (set 2026-09-28): finish NP-0, then NP-1.** NP-0 shrinks the
code every later slice touches, and NP-1 is both the largest remaining
"feels native" change and the surface NP-4 to NP-8 build on. Order:

1. **NP-0.8** first: every rebuild and every new pack miss costs about ten
   minutes of graphics preparation today, and NP-1 alone means dozens of
   rebuilds.
2. **NP-0.2** to **NP-0.5**, then the NP-0.6 repair of cards saved by older
   builds (its cause is already fixed).
3. **NP-1.1** to **NP-1.7**.
4. Between NP-1 items, the small measurable NP-2 items: **NP-2.1**,
   **NP-2.2** and **NP-2.8**, then NP-2.3 to NP-2.5.
5. Then NP-3, whose thread-placement and busy-poll work matters more now
   that the GPU commands thread has less to do.

NP-0.7 moved to NP-9.4, which bumps the pack format anyway.

Status on 2026-09-28: NP-0.1 to NP-0.5 and NP-0.8 are done, and NP-1.1 to
NP-1.4, NP-1.6 and NP-1.7 are done. NP-0.6's repair of cards saved by older
builds waits on a product decision, because it would change player save files.
NP-1.5 (the pause-menu entry) is in research; the settings screen opens with F6
until then.

## NP-0 Clean native baseline

**Why first.** The user-facing "renderer preview" caveat comes from carrying
Xenia machinery the native executor does not need, and every later slice
touches the same files. Removing what is dead, and naming what is not yet
native, shrinks the surface the performance and portability work has to
reason about. Only Xenos-emulation layers are removed here; the PM4 command
processor, register file, guest-memory mirror and the executor are the guest
GPU ABI and stay.

| Item | Work | Size |
| --- | --- | --- |
| NP-0.1 | **Done** (SDK `7ca40bf`): the translator bodies build only into the producer, `packet_disassembler` and `sampler_info` are gone, the SNR-M02 trace and the never-defined `d3d12_readback_*` declarations are removed, and the Vulkan-only readback and EDRAM-cache cvars are declared only in Vulkan builds (kept rather than deleted because non-Windows builds still compile the Vulkan backend until NP-12). The runtime DLL shrank 2,625,536 to 2,589,184 bytes; the producer rebuilt the 1x pack byte-identical. | S |
| NP-0.2 | **Done** (SDK `c0213b6`): no shader-hash literal remains on the draw path. The no-output skip now applies to any draw that tests and writes neither depth nor stencil, writes no color and exports no memory outside an occlusion query (it also catches the no-pixel-shader variant of the same point draws, so the pack loses one unused entry), and the linear video upload applies to every linear single-level unsigned 8-bit texture whose guest pitch matches the upload footprint. `fh1_scaled_32` and the reflection-cube import stay: they are keyed on texture layout, not shader identity. `check-fh1-constant-no-output.py` and `check-fh1-video-upload.py` are deleted; `check-fh1-scaled-32bpp.py` still checks the kept path. Golden replays byte-identical, `fh1-fmv` renders the movies, `fh1-race-sync` within run-to-run spread. | S–M |
| NP-0.3 | **Done** (SDK `bdc2e94`): `legacy` (a host query per ZPD begin and end, fenced at END) is the only occlusion path. The modern lifecycle (`ExecuteModernZPD`, `ZPDLifecycle`, the report and policy headers and their unit test), the `fake`, `fast` and `strict` modes, the `occlusion_query`, `zpd_end_policy` and `zpd_end_fallback` settings, the 17 `zpd_*` performance counters and `tools/qualify-zpd.ps1` are gone; about 1,240 lines left the SDK. Without host queries the fallback reports the fixed sample count at the END sentinel. Config schema 25 strips the three settings, and the launcher writes schema 25. Golden replays byte-identical; `fh1-modes-sync` and two `fh1-race-sync` runs pass with zero misses, GPU errors and executor skips, and the schema-21 seed config migrates to 25. | M |
| NP-0.4 | **Done**: `readback_resolve` stays as a developer setting (the executor's `full` mode is the reference for guest-visible resolves) that no launcher or default config writes. Migration and `set-graphics-experiment.ps1` drop it only from configs older than schema 24, which older launchers wrote for players, and keep it in current configs; [development findings](DEVELOPMENT.md) describe the modes. | S |
| NP-0.5 | **Done.** 101 research tools, 81 of their tests and three classifier configs from the retired static-world, track, vehicle, visibility, semantic, dispatch, lineage and Xenos-era census research are gone (recovery point `b59061f` in [RESEARCH.md](native-renderer/RESEARCH.md)). The GPU execution corpus, which had recorded nothing since `bccf126`, is removed with the pass tracker, the SDK `GraphicsFh1ExecutionKey` ABI, `rank-fh1-gpu-corpus.py` and the corpus parts of the render-test runner and discovery recorder; `pinyon_shift_fh1_gpu_corpus` stays as the switch for sampled native GPU timings. Found on the way: every graphics preparation since `bccf126` had written an empty startup pipeline allowlist, now built from the pipelines the preparation route creates (`b59061f`: 0 to 379 prewarmed pipelines, pipeline creations during the strict route 384 to 16). The shader capture no longer reports a Xenos fallback. | S |
| NP-0.6 | Striped car cards: the cause was found and fixed in XR-04 (SDK `0bf0658`): the cards are the profile's `Thumbnails/Thumbnail_N.xdc`, which the game renders, resolves and compresses from guest memory when it saves a car, and the renderer never copied that resolve back, so saved cards held stale memory. New cards are correct (about 34 KB); cards saved by earlier builds stay striped (275-845 KB of compressed noise) until the game saves them again. Remaining: find which game actions re-save a card (buying, painting, upgrading) and whether a missing card is re-rendered, then give players a repair: re-render stale cards through that path, or tell them which action fixes a card; any change to save files goes through a backup. [BUGS.md](../BUGS.md) is updated. | S–M |
| NP-0.7 | **Moved to NP-9.4**: prebuilt geometry shaders need a pack format bump, which NP-9.4 makes anyway. The runtime keeps `CreateDxbcGeometryShader` until then. | — |
| NP-0.8 | **Done** (SDK `bf7af69`). The disc corpus translates on one worker per logical processor (`--fh1_shader_production_threads`), and the per-file costs that dominated under real-time antivirus scanning are gone: the extractor writes one `corpus.blob` instead of 12,846 files, the capture appends bytecode to one `dxil.blob` instead of 24,700 files, and its manifest checkpoints at powers of two. A 1x production with recorded misses on 16 threads: corpus load 55 s to 33 ms, translation 105 s to 1.15 s, extraction 19 s to 7 s, whole preparation 6 min 36 s to 4 min 32 s, of which the two game routes are now about 4 minutes; packs byte-identical to the single-threaded producer. `tools/prepare-fh1-shaders.ps1` keys the pack and startup catalogs on the translator, shader-analysis, pipeline-description, pack and capture sources instead of the built binaries, so rebuilding host code, hooks or the renderer's command path no longer prepares graphics again. The game routes and incremental misses stay [pending](#faster-graphics-preparation). | S–M |

**Gates.** `rexgpu-fh1` compiles without translator bodies; every `REXCVAR_DECLARE` under `REX_HAS_D3D12` has a
definition; no `ucode_data_hash() == 0x…` literal under
`sdk/src/graphics/d3d12`; `tools/tests` pass; golden frame replays are
byte-identical; `fh1-race-sync`, `fh1-modes-sync` and `fh1-fmv` run with zero
pack misses and zero executor skips; `occlusion_query` has one behaviour and
the config writer and migration agree; frame-time medians within run-to-run
noise of the current baselines; car selection shows real car cards; a
rebuild that touches no graphics input launches without preparing, and the
parallel producer writes a pack byte-identical to the single-threaded one.

## NP-1 In-game settings and host UI layer

**Why second.** This is the single biggest "feels native" change and every
later slice needs an in-game surface: display settings (NP-4), profile and
dialogs (NP-5), input (NP-6), the trainer (NP-8) and mod UIs (NP-7). The
native UI4 insertion route stays blocked at scaler-binding registration
([UI API plan](UI_API_PLAN.md)), so the entry point uses the two proven guest
mutations (same-length label rewrite and an activation hook) and everything
past that row is host-drawn with the game's own assets.

| Item | Work | Size |
| --- | --- | --- |
| NP-1.1 | **Done**: `PinyonShiftApp` overrides `OnConfigureFonts` and `OnConfigureStyle` (`src/ui/host_style.cpp`): a 16-logical-pixel system font (Segoe UI on Windows, Arial on macOS, DejaVu Sans on Linux, the SDK font as fallback) rasterized at the window DPI so it stays sharp, and a charcoal, orange and magenta palette with roomier spacing for every ImGui surface, including the XAM message box, keyboard dialog, toast and F7 achievements list. The runtime log names the font and DPI scale. | S |
| NP-1.2 | **Done** ([UI assets](UI_ASSETS.md)): `tools/inspect-fh1-ui.py --decode-assets` catalogues the four UI archives, decodes all 4,373 `.xds` textures (RGBA8, BC1 to BC4 and DXT3A, untiled with the packed mip tail and swizzle) to PNG with a read-back check, byte-exact re-encoding for the 2,825 uncompressed ones, and records every vector font's metrics and glyph table plus the full `fontmap.xml`. Decision: neither bitmap fonts nor a TTF. The Latin fonts are GPU meshes (no installed TTF matches their advances within 5 %); their coverage rule was recovered from the title's shader and the host UI rasterizes the game's own fonts from the player's disc. | M |
| NP-1.3 | **Done**: `pinyon_shift::hostui::HostUi` (`src/ui/hostui/`) is a `UIDrawer` and window input listener on the SDK `ImmediateDrawer`. It reads `Fonts.zip` and `Horizon.zip` from the disc at first open (23 ms), rasterizes the vector fonts at the output size into glyph atlases, and uses the title's selection mask and button art. Layout is the title's 1280x720 space fitted into the painted guest output, which the SDK presenter now exposes (`GetPaintedGuestOutputRectFromUIThread`), so it scales with the internal resolution and keeps the 90 % safe area. Focus model with wrap-around and disabled rows; pad (d-pad, stick with repeat, A, B, Start), keyboard (arrows, numpad, Enter, Space, Escape, Backspace) and mouse (hover, click, wheel, right-click back). While open the title sees system UI (`xeXamSetHostUIActive`: `XN_SYS_UI` and `XamIsUIActive`, so it pauses into its own pause menu), and a `ReXApp` guest-input capture gives the guest untouched pads (now also for XInput) and no mouse-and-keyboard input, while `InputSystem::GetHostPadState` reads the pads for the menu under a new device lock. F6 (`bind_game_menu`) opens a SETTINGS screen with the live FULLSCREEN and MOUSE AND KEYBOARD toggles until NP-1.4 and NP-1.5 replace it. The guest-thread queue waits for NP-7.1; nothing here runs guest code. | L |
| NP-1.4 | **Done**: `src/ui/settings_menu.cpp` puts SETTINGS (F6 until NP-1.5) over the host UI with Display (fullscreen, vsync, frame-rate limit, variable refresh rate, game frame-rate limit), Graphics (resolution scale, anisotropy, anti-aliasing, motion blur, depth of field), Audio (master volume through the new `pinyon_shift_master_volume` and the SDK output gain, mute), Controls (mouse-and-keyboard mode and the key each pad control maps to) and Profile placeholders for NP-5. Live settings apply at once; the others save and show a RESTART badge that turns orange, with a note, while a saved change is not live yet (NP-1.6 makes the cheap ones live). Every change is written at once through `src/config/host_config.cpp`, with a backup in `config/backups` before the first save of a session. The launcher's edits moved into `tools/host-config.ps1` with the same rules, and `tools/tests/test_host_config.py` checks that both write identical bytes and that the launcher reads back what the game writes. No LOD-bias setting exists yet to expose. | M |
| NP-1.5 | Pause-menu entry: a production LSB2 label patch of the offline `MULTIPLAYER` row to `SETTINGS` (same byte length per language, or a per-language table) and an activation hook at the pause dispatch sites (`config/rexglue/analysis/main-xex.toml:135-140`) that opens the host screen and returns focus to the row on close. The stock row keeps its font, focus sound and animation. | M |
| NP-1.6 | **Done**: anti-aliasing (`swap_post_effect`, through a change callback to the command processor), the game frame-rate limit (`pinyon_shift_fh1_render_fps_limit`, reread every guest-vblank tick) and variable refresh rate (the D3D12 presenter recreates its swap chain when the tearing preference changes) apply live; only the resolution scale still needs a restart. F11 (`bind_fullscreen`) toggles fullscreen and saves it. Pad input in host menus is applied after drawing, so window and swap-chain changes never happen inside a paint. `fullscreen`, `vsync`, the presentation limit, anisotropy, motion blur, depth of field, keybinds and `mnk_*` were already live. | S |
| NP-1.7 | **Done**: the launcher's graphics panel keeps only the internal resolution (a scale needs shaders prepared before launch) and an OTHER SETTINGS button that explains the rest is in game (F6). `set-graphics-experiment.ps1 -Action Apply` writes only the settings it is given, so a launcher save no longer resets in-game choices, and it no longer forces `vsync = true`; `test_graphics_settings.py` covers a scale-only save keeping them. Install, verify, build, play and report were already the launcher's flow. | S |

**Gates.** The settings screen opens and closes 100 times in a scripted route
with no leaked component, stale callback or save write; it is navigable with
pad only, keyboard only and mouse only; it renders inside the 90 % safe area
at 1x, 2x and 3x; a TOML written in-game reads back identically in the
launcher; `fh1-pause.fh1test` passes three consecutive runs with no access
violation; hot settings apply within one frame and restart-required ones show
the badge; the only ImGui windows a player can reach are the XAM dialogs
pending NP-5.2.

## NP-2 Fast frame, pass 1

**Why now.** The race window is CPU-bound on the GPU Commands thread while
the GPU finishes in half the time. The items below are the evidence-backed,
low-risk parts of that path; the larger rewrites (resolve aliasing, direct
recording, thread parallelism) are NP-9. Instrument first: every item has a
counter or trace to prove its share before code changes.

| Item | Work | Size |
| --- | --- | --- |
| NP-2.0 | **Done.** Instrumentation: per-frame `texture_resolve_reloads` and `texture_resolve_reload_bytes` counters, `texture_reloads`/`texture_loads` phases in `--fh1_native_gpu_profile`, the back-face stencil mask in the frame census and `tools/summarize-fh1-stencil-census.py`; CPU profile captures now run on a private state copy with a staged pack. Race results are in the [performance baselines](native-renderer/NATIVE_PERFORMANCE_BASELINES.md#race-frame-cost-breakdown): the race frame is bound by the GPU commands thread (23.1 of 25.8 ms busy; the title polls for it 9.9 ms per frame); on that thread `IssueDraw` takes 11.6 ms, tape replay 3.9, type-0 register writes 3.7 and shared-memory uploads 3.7; resolve-sourced reloads are 88 per frame (64-71 MB, 0.39-0.56 ms GPU). | S |
| NP-2.1 | Cache the render-target binding across draws instead of recording `OMSetRenderTargets` on every draw (`fh1_native_executor.cpp:1816`); invalidate on transfers, clears and the swap compute. | S |
| NP-2.2 | Dirty-mask fetch constants by the shaders' used registers instead of re-uploading the whole 768-byte block on any write (`command_processor.cpp:1778-1780, 3845-3856`); reduce the per-register float-constant gather. | S–M |
| NP-2.3 | Persistent SRVs per executor surface instead of one-use descriptors created per resolve and transfer (`fh1_native_executor.cpp:1089-1111, 1915-1934`). | S |
| NP-2.4 | Cheaper depth-transfer stencil: the NP-2.0 census shows both scene depth surfaces write nonzero stencil (REPLACE with a per-object reference) in every race window, so precision tracking alone cannot skip the eight stencil-bit passes on the dominant 4x/1x ping-pong. Replace them with one pass that writes the stencil reference from the pixel shader where `PSSpecifiedStencilRefSupported` holds, keep the bit passes as the fallback, and keep the skip for sources the census proves unwritten (`fh1_native_executor.cpp:927-936, 1053-1082`); a single-pass fast path for same-layout MSAA-only depth transfers; skip transfers whose destination is cleared before use (needs a guest-order proof from a frame dump). | M |
| NP-2.5 | A/B quad, point and rectangle lists without geometry shaders using the existing `force_convert_quad_lists_to_triangle_lists` cvar; keep if menus and the map are unchanged under golden replay. | S |
| NP-2.6 | **Done** (SDK `e18b220`), as overlap rather than direct recording: direct recording would only move the 3.7 ms of runtime and driver time into `IssueDraw` on the same thread. A submission worker now replays each tape, executes it and signals the fence; frames split into a new submission every 1024 draws when no occlusion query is open, and the swap and every direct-queue operation of the GPU commands thread wait for the worker. `fh1-race-sync`: race-window median 21.05-21.19 ms synchronous vs 19.25-20.32 ms asynchronous (-5.5 %), p95 25.6 vs 22.3-25.2 ms; 4/4 golden replays. Controls: `--d3d12_async_submission=false`, `--d3d12_submission_split_draws=N`. One of seven async runs stalled before the title menus, a boot stall an earlier synchronous run also hit; watch its rate. | M |
| NP-2.7 | **Done** (SDK `37d0f72`): per-thread single-writer perf counters instead of two locked adds per increment, a reused range list in `SharedMemory::RequestRanges`, a known-register bitmap instead of the `GetRegisterInfo` switch on every register write, and sampler parameters reused while their fetch constant, binding filters and `anisotropic_override` are unchanged. `fh1-race-sync`, three interleaved pairs: race-window medians 25.28/24.30 ms control vs 21.13/21.21 ms on undisturbed runs, with equal per-frame draw, texture and pipeline counters. | S |
| NP-2.8 | Size the per-draw shared-memory uploads (`UploadRanges`, 2.27 ms per race frame, 1.69 ms of it memcpy): log bytes and ranges per frame, separate index, vertex and constant data, and decide whether CPU-written ranges can be uploaded once per frame or mapped instead of copied per request. | S |

**Gates.** Three-by-three control and candidate runs on `fh1-race-sync`
(last 600 frames) and `fh1-race-sustained` summarised with
`tools/summarize-performance.py --baseline`; target at least a 15 % lower
race-window median at 1x with p95 no worse; golden replays byte-identical or
within the documented tolerance; `fh1-buy-car` thumbnail still real; executor
stats show the expected drop in transfer tile-passes and descriptor requests.

## NP-3 Modern CPU, pass 1

**Why now.** The game was tuned for three in-order cores and six hardware
threads with a fixed 30 Hz cadence; the host gives it 1:1 threads, ignores
its priorities and affinities, and lets it busy-poll. The player-visible part
is NP-3.7: unlocked frame rate is the default, and some animations run fast.
Every numerics change is gated by the pose baseline and the save payload
hash, because gameplay integrates a variable delta.

| Item | Work | Size |
| --- | --- | --- |
| NP-3.0 | Prerequisites: instruction-level attribution for generated code (extend `tools/profile-etl-export` to emit `file:line` per sample and map generated lines back to guest addresses, which unblocks every "defer until instruction-level evidence" decision); name guest threads by start address and log `ExCreateThread` parameters; add ready-time (scheduler delay) and waker analysis to `tools/summarize-cpu-hotspots.py`; add kernel-side counters (watch faults, `VirtualProtect` calls and pages, global-lock acquisitions and contentions, critical-section spins, clock-mutex contention, timer-queue wakeups); include Microsoft symbols so kernel and CRT time can be attributed. | M |
| NP-3.1 | Thread placement: opt-in cvars to honour guest priorities and affinities (`ignore_thread_priorities` and `ignore_thread_affinities` default to true), prefer performance cores for the main guest thread, GPU Commands and GPU VSync on hybrid CPUs, and raise their priority; measure p95 and p99 on a hybrid machine and a four-core machine. | S |
| NP-3.2 | Replace the title busy-poll (`sub_829F04A8` polling the word written by `EVENT_WRITE_SHD`) with a targeted wake: the command processor signals a host event when it stores to the polled address and the hook blocks with a bounded timeout. This is a CPU, power and lower-core-count win more than a frame-time win; the naive one-millisecond sleep trial regressed and is the documented control. | M |
| NP-3.3 | Write-watch churn: batch the per-page `VirtualProtect` restores into runs, re-arm watches less often, and test 64 KiB watch granularity (`sdk/src/system/xmemory.cpp:2112-2293`, `sdk/src/graphics/shared_memory.cpp:366-399`); gate with `tools/check-fh1-texture-watch.py` and golden replays. | M |
| NP-3.4 | Lock diet: cache a direct object pointer with a generation in the guest dispatch header so `GetNativeObject`, `KeSetEvent` and waits skip the recursive global mutex and handle table; add a pause instruction and a spin cap to `RtlEnterCriticalSection`; replace the timer queue's spin-wait strategy; run with `clock_no_scaling=true` or make `UpdateGuestClock` lock-free. | S each |
| NP-3.5 | FMA3 build variant: measure a `-mfma` (x86-64-v3) build against the SSE4.1 baseline, then ship a dual baseline with runtime dispatch if it wins. Hardware FMA is bit-identical to `std::fma`; never substitute `a*b+c`. | S to test, M to ship |
| NP-3.6 | Replace the two 500 µs yield-spins (vblank thread and presenter) with high-resolution waitable timers and a spin of at most 50 µs; watch vblank lateness and dropped presents. | S |
| NP-3.7 | HFR correctness: locate the per-frame-stepped NPC and title-UI animation updaters (candidates: `sub_82AE8AE0`, which multiplies the video-mode refresh by a constant, and the consumers of the main-loop delta at `owner+448`), fix them with hooks that use real time, and extend `expect-simulation-time` to animation duration. | M–L |
| NP-3.8 | Deferred until NP-3.0 evidence exists: codegen register-locality options (`non_volatile_as_local`, `cr_as_local`, `ctr/xer_as_local`, blocked by interior-PC resume and fiber re-entry), `vmsum` and unaligned vector store lowerings, an AVX2 baseline. | L |

**Gates.** The fixed A/B protocol (seed `appdata-2026-09-27`,
`fh1-race-sync` last 600 frames, `fh1-race-sustained` window, three runs per
arm, frozen binary hashes); `expect-simulation-time 0.95 1.08`; pose-drift
gate on `fh1-timing-straight`; `M5_TRACE save.file.write payload_hash`
equality against control; NPC and title animation duration equal to real time
at 60, 120 and 144 Hz; process CPU seconds and per-core utilisation reported
alongside frame time.

## NP-4 Any display

**Why now.** Once settings live in-game, display options are the next thing a
PC player reaches for. Integer internal scales stay: the translator bakes the
scale into shader immediates and resolves address guest memory by integer
area, so non-integer or dynamic scale is an architecture change with no
payoff over "2x plus a good downscale". Ultrawide goes Hor+ through guest
hooks, the approach the Skate 3 fork shipped, before anyone considers a wider
back buffer.

| Item | Work | Size |
| --- | --- | --- |
| NP-4.1 | Output settings in the NP-1 screen: fullscreen, monitor, window size, letterbox and overscan. Live resize already recomputes the paint flow. | S |
| NP-4.2 | Presenter scaling quality: enable `REXGLUE_ENABLE_FIDELITYFX` (CAS and FSR 1) or add a Lanczos downscale so 2x on a 4K display and 3x on a 1440p display are sharp; expose `present_effect`. | S–M |
| NP-4.3 | 4x internal scale: lift the gates at `fh1_native_executor.cpp:408` and `command_processor.cpp:1040` and in the scripts, widen the two-bit scale field in the resolve constants (`fh1_native_executor.cpp:1952`) and its compute shader, add fallbacks for the reflection-cube import and video textures, and budget the 512 MB × scale² tiled resolve range. Produce and validate a 4x pack. | M |
| NP-4.4 | Ultrawide Hor+: guest investigation of the projection site (start from the device object written by `sub_829FD588` and read by `sub_829FC0A0`; `is_widescreen` is not consulted there), a projection hook that widens the horizontal FOV to the window aspect, report an ultrawide video mode so the presenter fills the window, and compensate the UI4 root X scale through the builder hooks so the HUD anchors to the edges. A FOV slider falls out of the same hook. | M–L |
| NP-4.5 | Frame cap and VRR: expose `host_present_fps_limit`, `pinyon_shift_fh1_render_fps_limit`, `vsync` and tearing in-game; verify a 30, 60 and 120 cap with the distinct-presentation test and VRR with tearing on. | S |
| NP-4.6 | Texture quality: anisotropy beyond the current 16x cap, a `MipLODBias` cvar and a force-trilinear override at `sdk/src/graphics/d3d12/texture_cache.cpp:1005-1074`. | S |
| NP-4.7 | Runtime internal-scale switch: re-initialise executor surfaces, the tiled resolve ranges and the pack without a restart, with packs for every offered scale produced up front. | L |
| NP-4.8 | Recorded decisions, not scheduled: non-integer or dynamic scale (architectural), a true wider back buffer (predicated tiling bands, every resolve kind, thumbnails; only if Hor+ shows unacceptable artifacts), HDR output (the guest tonemaps to SDR; plumbing is M, quality is L). | — |

**Gates.** 21:9 and 32:9 show more world horizontally with no distortion and
the HUD at the edges; 16:9 output is unchanged under golden replay; 2x on 4K
and 3x on 1440p pass a sharpness comparison against the pixel-exact case;
the 4x pack runs the race, free-roam and map routes with zero misses and the
memory budget logged; caps honoured within the distinct-presentation
tolerance.

## NP-5 Native profile, achievements, dialogs, language

| Item | Work | Size |
| --- | --- | --- |
| NP-5.1 | Profile cvars `user_name` and gamerpic path in the SDK `UserProfile` so `XamUserGetName` and `XamReadTileToTexture` return a real tag and tile; the XUID stays fixed by default because it names the save directory. Editable in the settings screen. | S–M |
| NP-5.2 | An `XamUiProvider` interface in `sdk/src/kernel/xam/xam_ui.cpp` with the built-in ImGui provider as default; the host layer implements the message box, keyboard, achievements list, gamercard and `XamShowMessageBoxUIEx` (a stub today), preserving overlapped and `XN_SYS_UI` semantics and the `XamIsUIActive` count. | M |
| NP-5.3 | Achievement toast and list on the host layer with XDBF icons and FH1 styling; unlock sound optional. Unlocks keep the existing TOML store. | S |
| NP-5.4 | Language: implement `XGetLanguage` from `user_language`, verify the `.str` suffix mapping for all 18 disc languages, extend the `XLanguage` enum where the disc has more, and fall back to English for unmapped tables. | M |
| NP-5.5 | Save backups in-game: snapshot the profile directory at safe points (after the game's own write completes, observed at the save hooks), keep N slots under the state root, and offer restore from the settings screen. Saves are raw files, so this is host-side copying with a manifest. Never touches the AppData save without the player's explicit restore. | S–M |
| NP-5.6 | Photo-mode export: write a PNG of the front buffer at internal resolution on a bind, reusing the frame-dump path; store under the state root with a timestamp. | S |

**Gates.** A guest `XamShowMessageBoxUI` shows the host-styled box and
returns the chosen index; `XamShowAchievementsUI` opens the list and returns
after close; booting in German, French and Japanese shows a localised pause
menu with achievement strings following; the gamertag appears where the game
renders it; a restored backup loads and the original file hash is unchanged.

## NP-6 Input

| Item | Work | Size |
| --- | --- | --- |
| NP-6.1 | Pad-to-pad remapping in `InputSystem` with per-user button and axis map cvars and a remap screen; merges with the SDL mapping database, rumble unaffected. | M |
| NP-6.2 | Keyboard prompt text: show the bound key next to each pad button in the settings screen. Keyboard glyph textures in the game's HUD need the texture replacement path from NP-10.3 and are linked there. | S |
| NP-6.3 | Haptics and modern pads: verify DualSense and Steam Input through SDL, expose rumble strength and trigger options, and document the Deck layout. | S |
| NP-6.4 | Mouse steering and sensitivity exposed (`mnk_mouse`, `mnk_sensitivity` exist). | S |

**Gates.** Swapping A and B in the remap screen changes pause navigation and
the race, persists across restart, and the SDL database still resolves
unmapped pads.

## NP-7 Mod host v1

**Design decisions.** Guest addresses are fixed by the supported XEX, so
`config/rexglue/analysis/main-xex.toml` is already an address registry; host
symbols are not stable. Mods therefore link against a versioned C ABI,
semantic names and cvars, never raw addresses or the PPC context. The ABI
adopts the shape the ReXGlue community already uses (`rex_mod_abi_version`,
`rex_mod_create`, `IModPlugin` with `OnCreateDialogs`, `OnModuleLaunched` and
`OnShutdown`, `ModHostContext`, `mod.toml` with `requires`, `load_after`,
`conflicts` and `game_version`, `enabled_mods` ordering) so mods and tooling
transfer between projects, and extends it with Pinyon hook points, the symbol
table and a guest-thread request queue. Runtime DLLs can only replace indirect
calls through the dispatch table; direct calls are C++ calls in the generated
code. The host executable therefore owns a fixed set of hook points and
publishes them.

| Item | Work | Size |
| --- | --- | --- |
| NP-7.1 | Hook-point registry and guest-thread request queue: keep declaring sites as `[[midasm_hook]]`, but each site's C++ implementation dispatches to runtime-registered callbacks keyed by a semantic id (`frame.tick`, `vehicle.pose_written`, `save.before_encrypt`, `ui.pause_button_constructed`, `file.open`); mods enqueue closures that are drained on `frame.tick` at `0x823EDA10` using the existing `CallGuestFunction` helper. Re-host `ApplyUiMutationExperiment` and the three UI experiments on it. | M |
| NP-7.2 | Semantic symbol table generated from `main-xex.toml` plus the known struct offsets (vehicle slot, pose, save body, career stage), keyed by XEX hash, exposed read-only through the ABI; a CI check fails when the TOML and the table disagree. | S |
| NP-7.3 | Plugin ABI and loader: `include/pinyon_mod.h` (C), `DynamicLibrary` loading from `<state>/mods/<name>/code/`, `mod.toml` validation, dependency and conflict checks, never unloaded; context vtable with guest read and write, `call_guest`, `find_symbol`, `subscribe`, `enqueue_guest_task`, `register_cvar`, `add_dialog`, `register_bind` and `log_event`. Document the direct-call limitation. | M |
| NP-7.4 | Whole-file asset overlay: register an overlay `HostPathDevice` for `mods/<name>/game` ahead of the base game device in `OnPostSetup` (the Skate 3 pattern); granularity is whole archives because the title reads them through a C `FILE*`. | S |
| NP-7.5 | Profile isolation and save tagging: when mods or cheats are enabled, `OnConfigurePaths` points `user_data_root` at `<state>/user-modded`; a sidecar `pinyon_shift_mods.json` written from `save.before_encrypt` records enabled mods, active cheats, the mod-set hash and the plaintext body hash; the launcher and settings screen show the badge; loading a tagged profile with mods disabled asks first. Config schema 25 with the migration entry. | S–M |
| NP-7.6 | Samples and docs: a telemetry HUD widget through the UI API, a symbol-library mod, an asset-override mod, `mods_src/` scaffolding with the shared CMake include, and `docs/MODDING.md` (ABI, hook points, overlay layout, save policy). | S |

**Gates.** A sample DLL adds a cvar, opens a host-layer dialog, subscribes
to `frame.tick`, calls a guest function through the queue and shuts down
cleanly; a replaced `media/stringtables/EN.zip` is observed through the
file-open observer with the stock file untouched when the mod is disabled;
enabling cheats creates `user-modded` and the AppData save hash is unchanged;
`tools/check-markdown-links.py` passes with the new document.

## NP-8 Cheat menu v1

**Cheapest first.** The trainer ships as a first-party mod on the NP-7 hook
registry and profile isolation so the ABI is dog-fooded before third parties
use it; it does not wait for the plugin loader or the asset overlay.
Post-effect toggles and movie skip exist. Teleport and
position freeze use the vehicle-pose hook the project already writes through;
time scale uses the simulation delta the project already observes; career
stage skip reuses the checkpoint seeding that exists as a test variable; the
save-body editor uses the plaintext hook plus the disc's own profile schema.
Credits, XP, wristband, weather, traffic, race state, rewind and camera have
no located function yet and need the discovery programme.

| Item | Work | Size |
| --- | --- | --- |
| NP-8.1 | Cheat cvars in a `Cheats` category (hot-reload): `cheat_time_scale` (scale `f31` before the store at `0x823EDB84`, clamped and logged), `cheat_freeze_vehicle` and stored teleport points on `0x82BC5A3C`, `cheat_skip_career_intro`, and the title's own `perfmode`, `fasttrackrender` and `trackfardistance` command-line parameters (`0x824F8150`) exposed as cvars. | S |
| NP-8.2 | Trainer screen on the host layer: pages Player, Vehicle, World, Graphics and Debug; controller navigation; the guest pauses while it is open; every toggle logs a diagnostics event; only available when profile isolation (NP-7.5) is active. | M |
| NP-8.3 | Save-body editor: locate the credits, XP and wristband keys in the serialised body at `0x82C666D0` using `media/profileschema/ForzaProfile.sch` and the existing keyed-record search in `SeedCareerCheckpointInSavePayload`, edit in place with the body size unchanged, and verify the title reloads the value. A host-side profile decoder tool that round-trips a `save-snapshots` capture byte-identically is the first deliverable. | M |
| NP-8.4 | Discovery programme: name at least one function with a register contract for credits, XP, wristband, time of day, weather, traffic density, race state, rewind and camera, using trace probes, `gamedb.slt` table names and the existing UI-trace workflow; register each in the symbol table. | L, ongoing |
| NP-8.5 | Follow-on cheats as NP-8.4 lands: freecam, infinite rewind, traffic density, time-of-day and weather lock, unlock all cars and events. | S each |

**Gates.** The trainer opens and closes 20 times in free roam with no leaked
input and no guest call from the UI thread (asserted); teleport to five stored
points with the camera following and no discontinuity storm in vehicle
telemetry; an edited credit value survives a save, reload and UI display on
the isolated profile; the race route still passes with cheats off.

## NP-9 Fast frame, pass 2

**Why after NP-2 and NP-3.** These are the rewrites that make the race
window GPU-bound and that the Vulkan executor in NP-12 depends on. NP-9.0
comes first because the resolve work rewrites the executor, and the
portability assessment wants the API-agnostic core proven behaviour-preserving
on Windows before it gains a second consumer.

| Item | Work | Size |
| --- | --- | --- |
| NP-9.0 | Split `Fh1NativeExecutor` into an API-agnostic core (surface keys, tile ownership, `ClaimTiles`, `PlanCopy`, `GetResolveSources`, the overwrite-rect interpreter, stats and skips) and a thin D3D12 surface and pipeline layer, validated by byte-identical replays. | M |
| NP-9.1 | Resolve output aliasing: resolve directly into the destination texture (or bind native-written ranges as SRVs keyed by destination range and write generation), keep the mirror write for one-off CPU readbacks and thumbnails, fall back to the mirror decode when the CPU touched the range, and present the front buffer from the native surface without `RequestSwapTexture`. This removes the encode, untile and copy round trip for 15 M resolve-sourced fetches and the front buffer every frame. | L |
| NP-9.2 | Direct D3D12 recording if NP-2.6 chose overlap rather than removal of the tape. | M |
| NP-9.3 | GPU Commands thread parallelism: PM4 decode, binding updates and `UploadRanges` on worker threads, or a decode-to-record pipeline, with the ordering constraints of write-watches and `EVENT_WRITE_SHD` visibility documented and tested. | L |
| NP-9.4 | Backend-neutral shader pack format v3: move `Fh1ShaderPack` out of the `d3d12` namespace, key identity by translator version, a device features hash and scale instead of vendor id, add prebuilt geometry shaders (moved from NP-0.7: generate the finite `GeometryShaderKey` set offline in the producer, store it in the pack, and remove `CreateDxbcGeometryShader` (`pipeline_cache.cpp:2578-3622`), `format/dxbc.h`, `DXBCChecksum.cpp` and `thirdparty/dxbc` from the runtime DLL), and update `tools/native-shader-pack.py` and [the pack contract](native-renderer/SHADER_PACK_FORMAT.md). | M |
| NP-9.5 | Decision point, native draw ABI: replacing the per-draw register-to-`PipelineDescription`, `SystemConstants` and `UpdateBindings` derivation with a native contract requires the pack to stop targeting Xenia's constant-buffer and root-signature layout, a new translator output. This is the real boundary between "Xenia backend with native surfaces" and a native renderer. Decide after NP-9.1 and NP-9.3 with measurements; do not start it on speculation. | XL |

**Gates.** Race window median at or below the GPU span plus 2 ms on the
baseline machine; resolve-sourced fetches served without an untile dispatch
on `fh1-race-sync` and `fh1-free-roam`; golden replays within tolerance;
`fh1-buy-car` thumbnail real at 1x and 2x; no new executor skip reasons; the
pack format version bumped and documented.

## NP-10 Content mods

| Item | Work | Size |
| --- | --- | --- |
| NP-10.1 | Per-member overrides: a repacker that writes an overlay archive with stored (method 0) members for overridden entries and pass-through LZX members for the rest (stock archives already contain stored entries), plus the generalised stream adapter from the `scene_insert` experiment for UI scenes. Prefer the repacker for data and textures. | M–L |
| NP-10.2 | Database and tunable patches: apply SQL patch scripts to a per-mod-set overlay copy of `media/db/gamedb.slt` at launch with the base hash recorded, and merge `physics.zip` and `gametunablesettings.zip` XML and INI by key. First check whether the title opens `media/db` loose or through an archive, which decides whether NP-10.1's repacker is a prerequisite. | M |
| NP-10.3 | Texture replacement: a BC3 import path in the texture cache (the tiled decoder exists only in `tools/fh1_texture_import.cpp`), hash-named dumps and a `mods/<name>/textures/` directory, hot rescan. Enables keyboard glyphs and HD texture packs. | L |
| NP-10.4 | Optional: Lua 5.4 bound to the mod ABI (symbols, hook points, cvars, guest queue) loaded from `mods/<name>/code/*.lua`, with a per-tick overhead budget. | L |

**Gates.** One tunable XML member and one `.bgf` overridden without whole
archive replacement; a modified `Data_Car` row visible in-game and stock
restored when disabled; one car card texture replaced and rendered correctly
at 1080p and ultrawide.

## NP-11 UI extension API, production

| Item | Work | Size |
| --- | --- | --- |
| NP-11.1 | Production adapter from the [UI API plan](UI_API_PLAN.md): semantic component registry, per-operation completion status, `SceneReady` from the native scene-open boundary and `SceneClosing` before guest objects are released, activation callbacks keyed by component id, host-layer backend for additive HUD widgets. | M |
| NP-11.2 | Variable-length string tables so labels are not limited to same-byte-length rewrites. | M |
| NP-11.3 | Native insertion research: recover how the animation loader registers a cloned owner's scaler bindings in `sub_8281BBA8`; only connect `AddMenuItem` to the native backend after the eight-row acceptance test passes. Runs in parallel and may never converge; nothing else depends on it. | L |

**Gates.** The plan's production-adapter checklist is green;
`pinyon_shift_fh1_ui_api_tests` extended; a sample mod adds a HUD widget and a
pause action without touching guest addresses.

## NP-12 Linux and Steam Deck

Ordering follows the portability assessment: Linux x86-64 isolates the one
hard problem, graphics, from any CPU-architecture risk, exercises the POSIX
layer that already exists, and targets RADV, the driver with the fewest
feature gaps. Build a Vulkan-native executor on the existing base-class seams;
do not introduce a general RHI.

| Item | Work | Size |
| --- | --- | --- |
| NP-12.1 | Host CMake and presets for Linux: `linux-amd64` preset, codegen path from the built `rexglue` target instead of `out/win-amd64/Release/rexglue.exe`, drop `-fasync-exceptions` and `/Brepro` off Windows, link `pthread` and `dl`, generalise the CPU baseline check. | S |
| NP-12.2 | Host sources off Win32: a POSIX crash reporter behind the diagnostics interface, SHA-256 through the vendored `thirdparty/crypto`, atomic rename through `std::filesystem`, env access through the platform layer, `Windows.h` out of the app. | S–M |
| NP-12.3 | Un-drift the Vulkan backend so `REXGLUE_USE_VULKAN=ON` compiles in the fork (base-class virtuals, ZPD, native guest-output registration, texture-cache base changes since `6db74f6`). | M |
| NP-12.4 | `vulkan::Fh1NativeExecutor` and `VulkanHostRenderConfig` on the NP-9.0 core: image surfaces with layout tracking, dynamic rendering, transfer and resolve pipelines, readback buffers, timestamp queries; replace the Vulkan render-target cache at the same call sites the D3D12 side uses. | L |
| NP-12.5 | Compile the 67 `fh1_*` fixed-function shaders and the two texture-cache compute shaders to SPIR-V with `dxc -spirv` from the same HLSL; port the FH1 texture-cache features (reflection-cube import, scaled 32-bpp, linear video upload). | S + M |
| NP-12.6 | SPIR-V shader pack on the NP-9.4 format: on-device producer through the vendored glslang builder (no external compiler), pack identity from the SPIR-V translator version and a features hash, miss recording and self-repair. | M |
| NP-12.7 | Tooling and launcher: shell and Python equivalents of setup, toolchain provisioning, SDK preparation, build, shader preparation and launch; artifact keys from the Vulkan device UUID; a CLI or TUI launcher; a documented desktop-build-then-copy-to-Deck workflow and a distrobox recipe for SteamOS. | M |
| NP-12.8 | Deck qualification: gamescope and Wayland presentation, 1280×800, the POSIX multi-object wait polling and `SCHED_FIFO` degradation measured and fixed if pacing regresses, controls layout. | M |

**Gates.** Disc-to-play on Ubuntu 24.04 and on a Deck with the documented
steps; the route matrix runs with zero executor skips on RADV; frame time
within an agreed margin of the Windows 1x baseline on comparable hardware.

## NP-13 macOS

| Item | Work | Size |
| --- | --- | --- |
| NP-13.1 | `mac-arm64` preset, ARM64 baseline, verification of the Mach-O `musttail` thunks for 76,502 functions under the small code model. | S |
| NP-13.2 | Guest-code correctness on ARM64: the render-test routes and save and load flows, hunting simde lane-order, denormal and `vmsum` NaN discrepancies; deterministic routes must match Windows outcomes. | M |
| NP-13.3 | MoltenVK validation of the Vulkan executor (portability subset gaps, sample-rate shading, storage-buffer range, MSAA depth resolve, timeline semaphores); record the Metal-via-MoltenVK baseline. | M |
| NP-13.4 | macOS tooling: Homebrew-pinned toolchain manifest, app bundle layout with dylibs beside the executable. | S–M |
| NP-13.5 | Conditional: a native Metal backend only if NP-13.3 shows unacceptable overhead or an unworkable gap. Not recommended by default. | XL |

## NP-14 Android

| Item | Work | Size |
| --- | --- | --- |
| NP-14.1 | SDK Android build: NDK toolchain, the missing `rex/main_android.h` glue, SDL3 activity and Gradle project, Android surface path. | L |
| NP-14.2 | Fibers without `ucontext`: a hand-written AArch64 context switch (also removes a syscall per switch on every POSIX target). | S–M |
| NP-14.3 | Page-size independence: make the `0xE0000000` host offset runtime-selected in `xmemory` and the generated `REX_PHYS_HOST_OFFSET`; also fixes Linux ARM64 16 KiB kernels. | M |
| NP-14.4 | Mobile GPU constraints: descriptor-indexing fallback, BC decode when compression is absent, storage-buffer bucketing, MSAA 2x emulation, Adreno and Mali workarounds. | L |
| NP-14.5 | Cross-build and sideload workflow: codegen and NDK cross-compile on the user's PC from their own ISO, on-device or PC-side pack production keyed by the device features hash, nothing derived distributed. | M |
| NP-14.6 | Performance and thermals on the reference device; touch and controller input; scale fixed at 1x. | L |

## NP-X Quality and tooling (ongoing)

- **CI compiles C++.** Build `rexruntime`, `rexgpu-fh1` and the four
  excluded test targets on Windows and, after NP-12.1, Linux; no game data is
  needed for them. Run `pinyon_shift_fh1_ui_api_tests`,
  `pinyon_shift_fh1_pass_tracker_tests` (or delete with NP-0.5),
  `pinyon_shift_fh1_shader_pack_tests` and `pinyon_shift_fh1_execution_key_tests`.
- **Performance gate.** Keep the three-by-three A/B protocol manual on the
  baseline machine until a fixed CI machine exists; publish the baseline
  summary JSON with every train.
- **Hardware qualification.** AMD, Intel and lower-end GPUs (XR-09) and an
  unscripted human drive (XR-08) before every train; these need people and
  hardware the project does not have and are tracked, not scheduled.
- **Determinism guardrails.** Pose baseline, save payload hash and golden
  replays are mandatory for any timing, numerics or resolve change.
- **Docs hygiene.** One focused document per retained contract; run-by-run
  logs under `.local`; `tools/check-markdown-links.py` in CI stays.

## NP-D Distribution and first run (ongoing)

The legal model does not change: nothing derived from the disc ships, the
user builds locally, packs stay local ([legal](LEGAL.md)). What changes is how
long and how Windows-specific that is.

- **First build time.** Cache the SDK and toolchain builds between source
  versions, use a compiler cache for the generated translation units, produce
  packs in parallel with the build, and measure; target under 20 minutes on
  an eight-core machine from the current 20–60.
- **Launcher core.** Extract a Python or CLI core (setup, verify, build,
  launch, report) that the WPF launcher calls today and that NP-12.7 reuses on
  Linux and macOS; a cross-platform GUI is a later option.
- **Signing and portability.** Sign the launcher and preview executables;
  support portable installs (both on the README roadmap).
- **Crash reporting.** Keep the sanitised bundle and prefilled issue; the
  POSIX reporter from NP-12.2 joins it.

## Pending to formalize

Accepted directions that still need research before they become numbered
slices with items, sizes and gates.

### Loading times on modern storage

Make boot, title-to-world, event entry and fast travel as short as a PC on
an SSD or NVMe drive allows, instead of paced for the Xbox 360's DVD and
memory budget.

- **Measure first.** Add load-phase markers (boot to title, title to free
  roam, event entry, event exit, fast travel) to the render-test JSONL and
  the critical-path trace, and record a baseline per phase on
  `fh1-race-sync` and a free-roam route.
- **Known evidence.** Host file I/O is small: an archived run read 189 MB
  in 4,538 reads for 91 ms of total read time, and 1,203 opens took 108 ms
  (`xboxkrnl_io.cpp` reads are synchronous; `HostPathDevice` does one
  `ReadFile` per call). Load time is therefore expected to sit in guest work
  rather than the drive: guest-code asset decompression, texture reloads (a
  single transition frame reloaded 75-350 textures and took 212-343 ms in a
  2026-09-28 play session), shader and pipeline availability, and title
  waits paced by frames or vblank.
- **Candidates once measured.** Unthrottled frame pacing while a loading
  screen is up (NP-3.7's variable-delta work may cover part); parallel or
  asynchronous `NtReadFile`/`NtReadFileScatter` and read-ahead of the
  archives a load touches; caching decompressed archives or assets on disk
  between runs; batching the texture uploads of a load; skipping the
  remaining intro and legal screens by default; and finding any minimum
  loading-screen durations in the title that exist only for disc streaming.
- **Guardrails.** Loads must still produce the same world state (vehicle
  pose baseline, save payload), and nothing may change the AppData save.

### Compile uncached shaders on the fly

A shader missing from the pack drops its draws until the next graphics
preparation: a 2026-09-28 play session hit 12 such shaders, and one of them
failed more than 32,768 draws before the session ended. The misses are
recorded (`cache/fh1-shader-misses`), but they only reach the pack when the
launcher reproduces the whole pack, which it does only when the build
changes.

- **Goal.** Translate and compile a missed shader in the background during
  play, draw it as soon as it is ready (the draw is skipped until then, as
  with a pipeline still being created), and keep the result in a local
  delta cache next to the pack so later sessions and later builds with the
  same translator version reuse it instead of recompiling.
- **Open questions.** NP-0.1 took the translator out of the runtime DLL, so
  on-the-fly translation needs the producer loaded on demand (or a separate
  translator module) rather than re-linking it into `rexgpu-fh1`; the delta
  cache has to be keyed like the pack (translator version, vendor, flags and
  scale) and should fold into NP-9.4's pack format v3; a preparation run
  should merge the delta into the pack and drop it; background compilation
  must not stall the GPU commands thread.
- **Gate idea.** A play session that hits a pack miss renders the missing
  draws within a few frames and logs no repeated `Failed in backend`
  errors, and the next launch loads those shaders from the delta cache
  without a preparation run.

### Faster graphics preparation

Preparing graphics ("Preparing graphics for 1x") took about ten minutes
on a modern machine, and it ran far more often than it needed to. NP-0.8
made translation parallel, removed the per-file costs and keyed
preparation on the graphics sources: a 1x preparation now takes 4 min 32 s,
almost all of it the two game routes, and only runs when graphics code,
settings, the driver or recorded misses change. The rest of this entry
stays pending.

- **Where the time goes** (a 1x production on 2026-09-28 after NP-0.8, 4.5
  minutes without rebuilding the producer; before it 6.6): shader
  extraction 7 s (was 17-19 s), producer run 2 min 4 s (the disc corpus
  loads in 33 ms and translates in 1.2 s, was 55 s and 1 min 45 s; the
  capture route is the rest), pack build 11 s, strict validation route
  2 min 3 s. A launcher run that also rebuilds the producer takes longer.
- **Done in NP-0.8.** Parallel translation (105 s to 1.15 s), one corpus
  file and one capture file instead of about 37,500 small files, and a
  preparation key on the graphics sources instead of the binaries.
- **Re-preparation after every new pack miss.** Each recorded miss changes
  the key and reruns everything for a handful of shaders; translate only
  the new misses and append them (this meets the on-the-fly compilation
  entry above).
- **The two game routes.** The capture route and the strict validation
  route replay about 9,000 frames each at the game's own pace while hidden.
  Run them unpaced, capture pipelines only when the catalog inputs change,
  and move the strict check to a shorter route or to the background after
  the game starts, keeping it as a gate for release packs.
- **Smaller steps.** Cache the extracted corpus by dump hash (7 s); build
  the pack from the capture in the producer instead of a Python pass
  (11 s); ship a prebuilt producer with releases (NP-D) instead of building
  it locally.
- **Gate idea.** A rebuild that does not touch the translator starts the
  game with no preparation; a first preparation finishes in under two
  minutes on an eight-core machine; packs stay byte-identical to the
  single-threaded producer.

## Parking lot

Ideas considered and not scheduled; add to a slice when a train has room.

- Local rivals and leaderboards from `GameplayLog` and the profile, replacing
  the dead Xbox Live rivals (all Live exports are stubs today).
- Rich presence for Discord and Steam.
- Import cars from *Forza Horizon 2* (README roadmap; needs NP-10 and asset
  format research).
- Accessibility: subtitle size and HUD scale through the UI4 root transform
  once NP-4.4 finds it.
- DualSense adaptive triggers and a Deck controls layout beyond defaults.
- HDR output and a true wider back buffer (recorded under NP-4.8).
- Frame generation: not planned; distinct rendered frames are the contract.

## Mapping to the original asks

| Ask | Slices |
| --- | --- |
| Confirm and remove remaining Xenos compatibility; port the rest to the native renderer | NP-0, NP-9.4, NP-9.5 |
| Optimise the renderer | NP-2, NP-9 |
| Optimise the CPU side for modern machines and multi-threading | NP-3, NP-9.3 |
| Any resolution, aspect ratio, ultrawide, modern graphics settings | NP-4 (settings surface from NP-1) |
| Replace ImGui with native menus, achievements, profile settings | NP-1, NP-5, NP-6 |
| Modding APIs, UI first | NP-7, NP-11, NP-10 |
| Cheat menu for playthroughs | NP-8 |
| Metal and Vulkan for Android and macOS later | NP-9.0, NP-9.4 as prerequisites; NP-12, NP-13, NP-14 |
| Additions | HFR correctness (NP-3.7), save backups and photo export (NP-5.5, NP-5.6), profile isolation for mods (NP-7.5), CI that compiles C++ (NP-X), first-build time (NP-D), parking lot |
