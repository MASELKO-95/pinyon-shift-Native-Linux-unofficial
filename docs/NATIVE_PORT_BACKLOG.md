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

Status on 2026-09-29: NP-0.1 to NP-0.5 and NP-0.8 are done, and NP-1 is
done, gate runs included. NP-0.6's repair of cards saved by older
builds waits on a product decision, because it would change player save files.
NP-2.1, NP-2.2 and NP-2.8 are done; NP-2.3 to NP-2.5 were measured and not
built (NP-2.4 waits for AMD or Intel hardware). NP-3.2, NP-3.4 and NP-3.6 are
done, NP-3.1 is built and off by default, NP-3.0 and NP-3.3 are done in part.
NP-4.1, NP-4.2 and NP-4.6, NP-5.1 to NP-5.6 and NP-6.1, NP-6.2 and NP-6.4
are done. See [Needs a person](#needs-a-person) for what waits on hardware or
a decision.
Open from the 2026-09-29 play test: NP-2.9 (cinematic artifacts) and the
car-purchase case of NP-3.7; NP-1.8 (settings screen slowdown) is fixed.

## Needs a person

Work the autonomous passes cannot finish, with what each needs. Everything
else in a row marked done was checked by scripted routes, replays or A/B
runs on the development machine (Ryzen 7 5800X, RTX 4080).

| Item | What is left | Needs |
| --- | --- | --- |
| NP-0.6 | Repairing car cards saved striped by older builds changes player save files | The maintainer's decision |
| NP-2.4 | The one-pass `SV_StencilRef` depth transfer (patch in `.local/np24/`) | An AMD or Intel GPU to qualify it |
| NP-2.9 | Whether the in-game blue-wristband cinematic still shows green and pink | A play-through to the next wristband, or a save just before one |
| NP-3.1 | Latency-critical thread placement on P-cores and E-cores | A hybrid Intel CPU |
| NP-3.7 | Which animation plays too fast after buying a car before a race at the unlocked rate | A short clip or the moment it happens, from the player who saw it |
| NP-4.5 | That VARIABLE REFRESH RATE runs the display at the game's rate without tearing artifacts | A VRR (G-SYNC or FreeSync) display |
| NP-6.3 | DualSense and Steam Input through SDL, a Deck controls layout | Those controllers and a Steam Deck |
| NP-10.4 | Lua 5.4 for script mods (optional) | Adding Lua as a new vendored dependency: the maintainer's call |
| NP-X | AMD, Intel and lower-end GPU qualification; an unscripted drive before each train | Hardware and a player |

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
| NP-1.5 | **Done**: the offline pause menu's MULTIPLAYER row reads SETTINGS and opens the settings screen. Label: when `pausemenu.str` loads, only entry `0xDD6B` (`IDS_Multiplayer`) is rewritten, in place and only where it fits (the title reads strings up to the NUL; `IDS_MultiplayerOption` is left alone). Activation: a midasm hook on case 6 of `CPauseMenu`'s action switch (`sub_82739D00`, 0x82739DB0, vtable 0x8205109C checked) opens the screen on the UI thread and jumps to the switch's common exit (0x8273A10C), skipping the "MULTIPLAYER UNAVAILABLE" popup; the pause menu stays open with the row focused. Closing keeps the guest's input captured until the closing press is released, so B or Enter does not also resume the title. `pinyon_shift_pause_settings=false` restores the stock row. `fh1-pause.fh1test` passes three consecutive runs. | M |
| NP-1.6 | **Done**: anti-aliasing (`swap_post_effect`, through a change callback to the command processor), the game frame-rate limit (`pinyon_shift_fh1_render_fps_limit`, reread every guest-vblank tick) and variable refresh rate (the D3D12 presenter recreates its swap chain when the tearing preference changes) apply live; only the resolution scale still needs a restart. F11 (`bind_fullscreen`) toggles fullscreen and saves it. Pad input in host menus is applied after drawing, so window and swap-chain changes never happen inside a paint. `fullscreen`, `vsync`, the presentation limit, anisotropy, motion blur, depth of field, keybinds and `mnk_*` were already live. | S |
| NP-1.7 | **Done**: the launcher's graphics panel keeps only the internal resolution (a scale needs shaders prepared before launch) and an OTHER SETTINGS button that explains the rest is in game (F6). `set-graphics-experiment.ps1 -Action Apply` writes only the settings it is given, so a launcher save no longer resets in-game choices, and it no longer forces `vsync = true`; `test_graphics_settings.py` covers a scale-only save keeping them. Install, verify, build, play and report were already the launcher's flow. | S |
| NP-1.8 | **Done** (SDK `60455b0`). Cause: without vsync the guest vblank ticked at 1 kHz, and FH1 steps its simulation once per two vblanks (the play-test log shows 50-80 steps in each 110 ms frame against 1-4 normally), so turning vsync off ran 500 simulation steps a second; where a step is expensive, as on race central's paused screens, each frame spanned ever more vblanks. Host vsync now only changes presentation, and the vblank follows the render limit or twice the display refresh either way (a player who wants more sets GAME FRAME RATE LIMIT). Repro and check: `hostkey` steps in render-test routes (SDK `28635b1` `Window::InjectKey`) open SETTINGS and turn vsync off in free roam; before, 10 vblanks and 5 simulation steps per frame; after, 2.3 and 1.15, with the vblank still at 240 Hz. | S–M |

**Gates.** The settings screen opens and closes 100 times in a scripted route
with no leaked component, stale callback or save write; it is navigable with
pad only, keyboard only and mouse only; it renders inside the 90 % safe area
at 1x, 2x and 3x; a TOML written in-game reads back identically in the
launcher; `fh1-pause.fh1test` passes three consecutive runs with no access
violation; hot settings apply within one frame and restart-required ones show
the badge; the only ImGui windows a player can reach are the XAM dialogs
pending NP-5.2.

**Gate runs 2026-09-29** (`config/render-tests/fh1-settings-gate.fh1test`,
checked by `tools/check-fh1-settings-gate.py`): three consecutive passes from
the `appdata-2026-09-27` seed. Each run opens and closes SETTINGS 100 times
with F6 and Escape, reaches DISPLAY with only the keyboard, only the pad and
only the mouse (`hostkey` and `hostclick` route steps), and then resumes free
roam and drives 112 world units, so no drawer, input listener or guest input
capture leaks: 103 `hostui.open` and 103 `hostui.closed` events, no settings
file written. Every `hostui.layout` lies inside the 90 % safe area at a 4K
fullscreen output (canvas scale 3) and a 1600x900 window at 2x internal
scale (1.25); the layout follows the painted output rectangle, so the
internal scale cannot move it. The launcher read-back and the badge are
covered by `test_host_config.py` and NP-1.4 and NP-1.6, `fh1-pause` by
NP-1.5. Player-reachable ImGui is now the XAM dialogs (NP-5.2) and the F7
achievements list and toast (NP-5.3); F3, F4 and the console are developer
overlays.

## NP-2 Fast frame, pass 1

**Why now.** The race window is CPU-bound on the GPU Commands thread while
the GPU finishes in half the time. The items below are the evidence-backed,
low-risk parts of that path; the larger rewrites (resolve aliasing, direct
recording, thread parallelism) are NP-9. Instrument first: every item has a
counter or trace to prove its share before code changes.

| Item | Work | Size |
| --- | --- | --- |
| NP-2.0 | **Done.** Instrumentation: per-frame `texture_resolve_reloads` and `texture_resolve_reload_bytes` counters, `texture_reloads`/`texture_loads` phases in `--fh1_native_gpu_profile`, the back-face stencil mask in the frame census and `tools/summarize-fh1-stencil-census.py`; CPU profile captures now run on a private state copy with a staged pack. Race results are in the [performance baselines](native-renderer/NATIVE_PERFORMANCE_BASELINES.md#race-frame-cost-breakdown): the race frame is bound by the GPU commands thread (23.1 of 25.8 ms busy; the title polls for it 9.9 ms per frame); on that thread `IssueDraw` takes 11.6 ms, tape replay 3.9, type-0 register writes 3.7 and shared-memory uploads 3.7; resolve-sourced reloads are 88 per frame (64-71 MB, 0.39-0.56 ms GPU). | S |
| NP-2.1 | **Done** (SDK `6220b1b`), in `DeferredCommandList` rather than the executor so every caller benefits: a render-target bind equal to the tape's current one is not recorded, and `Reset`/`Swap` forget the binding because each tape replays into its own command list. 98 % of binds were repeats (11.98 of 12.22 million on `fh1-race-sync`). Five interleaved pairs: race-window median 20.08 ms off vs 19.70 ms on (-1.9 %); golden replays 4/4. Control: `--d3d12_elide_repeated_render_target_binds=false`. | S |
| NP-2.2 | **Done** (SDK `e999f5d`), measured first on `fh1-race-sync`. Fetch constants: 11.19 million block uploads for 12.2 million draws, and only 3 % followed writes to slots the draw's shaders do not read, so a dirty mask by used slots would save almost nothing and was not built. Float constants: 19.4 million uploads gathered 436 million registers in 136 million contiguous runs (about 22 in 7 per upload); the gather now copies whole runs, a third of the copies, with identical buffers (golden replays 4/4). The saving, about 0.1 ms per race frame by the counts, is below what the race A/B resolves. | S–M |
| NP-2.3 | **Measured, not built.** A temporary timer around the one-use descriptor request and `CreateShaderResourceView` calls for resolve and transfer sources (`fh1_native_executor.cpp` `CreateTransferSourceViews`, `ResolveToMemory`) on `fh1-race-sync`: 0.06-0.085 ms per race frame for about 110 view sets (about 0.7 us each, descriptor request included). Persistent per-surface views would still need a copy into the shader-visible ring per use, so the saving is a few hundredths of a millisecond, below what the race A/B resolves. | S |
| NP-2.4 | Cheaper depth-transfer stencil: the NP-2.0 census shows both scene depth surfaces write nonzero stencil (REPLACE with a per-object reference) in every race window, so precision tracking alone cannot skip the eight stencil-bit passes on the dominant 4x/1x ping-pong. Replace them with one pass that writes the stencil reference from the pixel shader where `PSSpecifiedStencilRefSupported` holds, keep the bit passes as the fallback, and keep the skip for sources the census proves unwritten (`fh1_native_executor.cpp:927-936, 1053-1082`); a single-pass fast path for same-layout MSAA-only depth transfers; skip transfers whose destination is cleared before use (needs a guest-order proof from a frame dump). **Measured 2026-09-28 on an RTX 4080:** NVIDIA reports `PSSpecifiedStencilRefSupported` = no, so the one-pass stencil export helps only AMD and Intel; it was built (an `SV_StencilRef` variant of `fh1_native_transfer_from_words.ps.hlsl` and a `kTransferDestDepthStencil` pipeline) but not committed, because WARP cannot replay the frame dumps (the pack is keyed to the NVIDIA device's shader modifications) and no AMD or Intel machine was available to qualify it; the patch waits for NP-X hardware qualification. Tracking the stencil bits each depth surface may hold, to run passes only for those bits, saved 3 % of the race dump's transfer tile passes: color-to-depth transfers, among the largest pairs, make all eight bits possible. Neither is worth landing without that hardware. | M |
| NP-2.5 | **Measured, not adopted.** The frame census of the golden dumps shows quad lists only in 3D scenes (about 660 draw records in `race-4500`, 580 in `free-roam-2000`, none in the title or photo dumps), rectangle lists everywhere (27-95) and 3 point lists; only quads have a switch (`force_convert_quad_lists_to_triangle_lists`), since points and rectangles would need vertex-shader expansion and new pack variants. Converting quads: `fh1-race-sync`, three interleaved pairs, race-window median 17.55 ms off vs 17.44 ms on (-0.7 %, within noise), and the race and free-roam golden replays change (58,884 and 196,547 differing words: the triangle split interpolates across a different diagonal than the geometry shader). No speed to gain, so the geometry-shader path stays. | S |
| NP-2.6 | **Done** (SDK `e18b220`), as overlap rather than direct recording: direct recording would only move the 3.7 ms of runtime and driver time into `IssueDraw` on the same thread. A submission worker now replays each tape, executes it and signals the fence; frames split into a new submission every 1024 draws when no occlusion query is open, and the swap and every direct-queue operation of the GPU commands thread wait for the worker. `fh1-race-sync`: race-window median 21.05-21.19 ms synchronous vs 19.25-20.32 ms asynchronous (-5.5 %), p95 25.6 vs 22.3-25.2 ms; 4/4 golden replays. Controls: `--d3d12_async_submission=false`, `--d3d12_submission_split_draws=N`. One of seven async runs stalled before the title menus, a boot stall an earlier synchronous run also hit; watch its rate. | M |
| NP-2.7 | **Done** (SDK `37d0f72`): per-thread single-writer perf counters instead of two locked adds per increment, a reused range list in `SharedMemory::RequestRanges`, a known-register bitmap instead of the `GetRegisterInfo` switch on every register write, and sampler parameters reused while their fetch constant, binding filters and `anisotropic_override` are unchanged. `fh1-race-sync`, three interleaved pairs: race-window medians 25.28/24.30 ms control vs 21.13/21.21 ms on undisturbed runs, with equal per-frame draw, texture and pipeline counters. | S |
| NP-2.8 | **Done** (SDK `54e96a7`, config schema 26). `SharedMemory` now counts uploads by kind (vertex, index, texture, memexport, other) with the bytes of pages already uploaded in the same frame, in the executor report. On `fh1-race-sync` a race frame uploaded about 20 MB of vertex data in 630 requests, 2.7 MB of index data and 2.5 MB of texture data, only 0.3 % of it twice in one frame: `clear_memory_page_state = true` dropped the valid bit of every CPU-uploaded page at each frame end, so every page a draw touched was copied again. Schema 26 turns it off (the Skate 3 recomp defaults it off too) and migrates older configs; over the route, vertex uploads fall from 41.6 GB to 0.42 GB and index uploads from 5.5 GB to 67 MB. Three interleaved pairs: race-window median 20.27 ms on vs 17.80 ms off (-12.2 %), p95 24.82 vs 25.26 ms. Race, photo-mode, buy-car (car-card render) and FMV routes pass with it off, with capture differences only in timing. | S |
| NP-2.9 | Green and pink artifacts in race central, only during the blue wristband collect cinematic (play test 2026-09-29, Release `d63d14f`). **Narrowed 2026-09-29, not fixed.** The story movies can be played without a save near a wristband: a local link mirror of the game files whose `PressStart.wmv` and `.def` are a story movie, run through `run-fh1-render-test.py --game-root`. FMV_02 (the VIP-lounge scene, the likely blue-wristband cinematic) plays cleanly for 50 s. FMV_04 shows about 0.8 s of solid green (0, 77, 0) over the whole output, logo included, when the movie loops, the same with `clear_memory_page_state` true or false and at a 30 fps render limit, so neither NP-2.8 nor the frame rate causes it. The per-frame census shows the title skipping its YUV draw (VS `7156CE05`/PS `31511D87` with the three 8-bit planes) for those 35 output frames while its composite pass (`20A41D46`/`5F479FC4`) still draws. Next: a frame dump of a green frame to find which surface the composite reads, whether the console would show the same (zeroed planes also convert to green there), and whether an in-game start of FMV_02 goes green before its first decoded frame; a play-through to the next wristband with a render test would confirm the in-game case. | S |

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
| NP-3.0 | Prerequisites: instruction-level attribution for generated code (extend `tools/profile-etl-export` to emit `file:line` per sample and map generated lines back to guest addresses, which unblocks every "defer until instruction-level evidence" decision); name guest threads by start address and log `ExCreateThread` parameters; add ready-time (scheduler delay) and waker analysis to `tools/summarize-cpu-hotspots.py`; add kernel-side counters (watch faults, `VirtualProtect` calls and pages, global-lock acquisitions and contentions, critical-section spins, clock-mutex contention, timer-queue wakeups); include Microsoft symbols so kernel and CRT time can be attributed. **Partly done 2026-09-29:** guest threads are named `Guest <start address>` and each creation logs its start, context, XAPI routine, stack, flags and CPU (SDK `a33518a`); contended global-lock acquisitions and write-watch triggers, protection calls and pages are counted (`4c77124`, `88213fe`); `.local/np3/thread-cpu.ps1` samples per-thread CPU by thread name during a run. Still open: `file:line` attribution in `profile-etl-export`, ready-time and waker analysis, clock-mutex and timer-queue counters, Microsoft symbols. | M |
| NP-3.1 | **Built, off by default; hybrid measurement waits for hardware** (SDK `3c50572`). `latency_critical_thread_placement` raises the main guest thread, GPU Commands and GPU VSync above normal priority and asks for the most performant CPU sets on hybrid CPUs; honoured guest affinities (`ignore_thread_affinities=false`) map each Xenon hardware thread to its own host physical core, most performant first, instead of logical processor N. `fh1-race-sync`, three interleaved pairs each: on the 8-core Ryzen 7 5800X p95 32.99 to 30.52 ms and median 28.89 to 28.61 ms (a noisy session); restricted to four physical cores (affinity 0xFF, `.local/np3/four-core.ps1`) median 16.85 to 17.12 ms and p95 21.14 to 24.88 ms. No consistent win, so it stays opt-in. Still to measure: a hybrid (P-core and E-core) machine, where the CPU set preference is the point | S |
| NP-3.2 | **Done** (SDK `96c24ce`). `sub_829F04A8` is the predicate seven D3D fence-wait loops (`sub_823E91F0` among them) call while the fence word, at `0xFFCA4000` in the 0xE0000000 physical view, has not reached their target; it ran about a billion times per `fh1-race-sync` run. The runtime now counts the command processor's CPU-visible write packets and wakes waiters (`rex::system::WaitForGpuWrite`); the midasm hook `PinyonShiftGpuFenceWait` at the predicate's entry spins 20 us and then blocks until the next write, bounded to 1 ms so the predicate's own timeout and kick logic keep running. About 100,000 waits per run, 88 % ended by the signal. Three interleaved pairs: process CPU over the route 186 to 159 CPU seconds (-15 %), race window 3.40 to 2.88 cores; race-window median 16.89 vs 16.80 ms, p95 21.38 vs 21.23 ms; `fh1-modes-sync` passes. Control: `--pinyon_shift_block_on_gpu_fence=false`. | M |
| NP-3.3 | **Done in part** (SDK `88213fe`): releasing a watch restored protection with one `VirtualProtect` (and TLB shootdown) per 4 KiB page, up to 1,024 pages; runs of pages that end with the same access now take one call. New per-frame counters (`write_watch_triggers`, `write_watch_protect_calls`, `write_watch_protect_pages`) show about 60-70 protection calls for 830 pages per `fh1-race-sync` race frame. Golden replays 4/4. Not done: re-arming less often and 64 KiB watch granularity, which change when the renderer sees CPU writes and need the texture-watch lifecycle check (`tools/check-fh1-texture-watch.py` cannot run its compiled harness here) and a race-window A/B | M |
| NP-3.4 | **Done** (SDK `4c77124`) except the direct object pointer: the timer queue thread yield-looped with nothing queued (about 5 CPU seconds a run; Windows only queues the 1 ms `KeTimeStampBundle` tick), and is now created on first use, blocks while empty and sleeps on the high-resolution timer until the next item is due; guest clock queries are lock-free (a published linear segment of host ticks and an atomic maximum); `RtlEnterCriticalSection` reads before each compare-exchange, pauses, and stops after 1,024 tries instead of up to 65,280. `fh1-race-sync`, two clean interleaved pairs against the previous build: process CPU 155 to 146 CPU seconds, GPU VSync thread 1.4 to 0.03, timer thread 4 to 0.4, frame medians unchanged, golden replays 4/4. The global lock is contended about 1.3 million times a run (about 260 per frame, now counted); caching object pointers in the dispatch header to skip it needs a safe-reclamation design and is left for when those contentions show on the critical path | S each |
| NP-3.5 | **Done**: the generated code writes the Xenon's fused multiply-adds as `std::fma`, a CRT call on the SSE4.1 baseline and one instruction with FMA3, bit-identical either way; it contains no separate multiply-add expressions a compiler could fuse, and the new `fma` baseline also sets `-ffp-contract=off`. Two interleaved three-pair `fh1-race-sync` A/Bs: the guest main thread uses 6-7 % less CPU (25.5 to 23.8 s, 24.1 to 22.7 s) and process CPU 1-6 % less, while the race-window frame time is unchanged within noise, bound by the GPU Commands thread. Players build locally, so instead of runtime dispatch `build-preview.ps1 -CpuBaseline auto` picks `fma` when Windows reports AVX2 (every AVX2 CPU has FMA3) and records it in `build.json`; `src/cpu_baseline_guard.cpp` stops a copied FMA build on a CPU without FMA3 with a message. Verified: the automatic preview build chose `fma` and passes `fh1-race-sync` | S to test, M to ship |
| NP-3.6 | **Done** (SDK `41c37c6`). The old vblank wait barely spun (about 11 us per wait): `sleep_for` overshot its 500 us margin, so vblanks woke about 600 us late on average and up to 2 ms. `rex::thread::SleepUntil` waits on a per-thread high-resolution waitable timer set early by the spin margin (50 us) plus a running estimate of the timer's own overshoot, then spins with `YieldProcessor`; the vblank thread and the presenter pacing use it. `fh1-race-sync` at the unlocked 240 Hz vblank: lateness 43 us mean and 0.3-1.2 ms max, about 92 us of spin per wait (2 % of a core), race-window median 16.91 vs 16.93 ms; with `host_present_fps_limit=60` the present interval median is 16.92 vs 17.21 ms. Control: `--high_resolution_timer_waits=false`. | S |
| NP-3.7 | HFR correctness: locate the per-frame-stepped NPC and title-UI animation updaters (candidates: `sub_82AE8AE0`, which multiplies the video-mode refresh by a constant, and the consumers of the main-loop delta at `owner+448`), fix them with hooks that use real time, and extend `expect-simulation-time` to animation duration. Reported in the 2026-09-29 play test: buying a car before a race plays its animation too fast at the unlocked frame rate.. **Measured 2026-09-29, not fixed**: output-frame-paced captures of the purchase at a 30 fps limit and unlocked (about 112 fps; `.local/np37/`) differ at equal frame indices after a game-state sync: unlocked reaches the showroom's close beauty camera by frame 2668 while the 30 fps run still holds the standard view at 2684, and the loading screens after the purchase advance with wall time. That fits neither a purely per-frame nor a purely timed animation, so which animation the player saw run fast is the next thing to pin down | M–L |
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
| NP-4.1 | **Done**: Display has MONITOR, WINDOW SIZE (default, 720p to 2160p) and ASPECT RATIO (letterbox, crop into the overscan margin, stretch) next to FULLSCREEN, all saved through the host config with the restart badge where the SDK reads them at start | S |
| NP-4.2 | **Done** (SDK `fabe814`): the CAS and FSR 1 presenter shaders ship prebuilt, but were gated on the FidelityFX SDK that only temporal FSR 2/3 needs; the gate is now always set (publicly, so every target sees one presenter layout) and Display has OUTPUT SCALING: bilinear, CAS (sharpen, resample when downscaling) and FSR 1 (EASU with RCAS). A sharpness comparison of 2x on 4K and 3x on 1440p against the pixel-exact case is still to run | S–M |
| NP-4.3 | **Done** (SDK `c18f1d6`, `1e13348`): 4x lifts the scale gates, widens the resolve scale field and its shader, and budgets the tiled resolve range; the launcher, SETTINGS, preparation, the artifact producer and the pack tool accept it. A 535 MB 4x pack prepared after three miss-recording rounds: `fh1-race-sync` passes with no misses or errors; `fh1-free-roam` and `fh1-map` run clean and fail only their timing checks at 4x | M |
| NP-4.4 | Ultrawide Hor+: guest investigation of the projection site (start from the device object written by `sub_829FD588` and read by `sub_829FC0A0`; `is_widescreen` is not consulted there), a projection hook that widens the horizontal FOV to the window aspect, report an ultrawide video mode so the presenter fills the window, and compensate the UI4 root X scale through the builder hooks so the HUD anchors to the edges. A FOV slider falls out of the same hook.. **Investigated 2026-09-29, not built**: a PM4 replay of the `free-roam-2000` golden dump (type-0 register writes, `SET_CONSTANT`, `LOAD_ALU_CONSTANT`) finds a pure 16:9 perspective matrix (x scale -1.2487, y -2.2199) in vertex constants c20-c23 and c4-c7, written by type-0 packets covering c0-c47; scaling its x term on the host as it is written changes nothing on screen. The same block holds the combined matrices one row per input axis (view-projection c0-c3, world-view-projection c28-c31; clip x is each row's `.x`, and z equals w in rows 0-2 of a perspective matrix); scaling that `.x` column in every such block narrowed the player's car as expected but left the terrain, road and trees unchanged, so world geometry gets its matrices through another write, and the game's own frustum culling would still cut the widened edges. A host-side Hor+ is therefore not the short path: the guest camera's field of view (so culling agrees) is | M–L |
| NP-4.5 | **Done** except VRR, which needs a VRR display: Display has VSYNC, FRAME RATE LIMIT (`host_present_fps_limit`) and VARIABLE REFRESH RATE (tearing), Graphics has GAME FRAME RATE LIMIT (`pinyon_shift_fh1_render_fps_limit`). `fh1-free-roam`, median over the route: game limit 30, 60 and 120 give 30.1, 59.4 and 117.5 fps with no duplicate presentations and a simulation-to-wall ratio of 1.01; the host limit at 60 gives 60.0 fps. Loading and the heaviest frames stay below the higher caps | S |
| NP-4.6 | **Done** (SDK `270243b`): `force_trilinear_filtering` (live, part of the sampler reuse key) blends mips for textures the title samples linearly with point mip selection, and `texture_mip_lod_bias` (restart; samplers are cached) offsets every texture's mip level; Graphics exposes both. Anisotropy cannot go past 16x: that is the D3D12 maximum | S |
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
| NP-5.1 | **Done** (SDK `7ca8128`): `user_name` (sanitised to an Xbox gamertag, restart) replaces the fixed "User" and FH1 reads it through `XamUserGetName`; `user_gamerpic` fills `XamReadTileToTexture` tiles from a PNG or JPEG. The XUID and save directory stay fixed. Profile edits the gamertag one letter at a time (LETTER, POSITION, DELETE, SAVE) so the pad works as well as the keyboard. Where FH1 shows the name in-game is not yet confirmed | S–M |
| NP-5.2 | **Done** (SDK `165d1b8`, project `2fd9ec4`): `rex::kernel::xam::XamUiProvider` lets the host replace the ImGui message box and keyboard; the dispatcher keeps the `XN_SYS_UI` notifications, the `XamIsUIActive` count and the overlapped completion, and B or Escape completes with `X_ERROR_CANCELLED`. The host UI draws both in the game's fonts: wrapped text under the title, buttons as rows, and a keyboard edited with the pad (letter, position, delete) or by typing. `fh1-xam-dialogs` (sample dialogs through `xamdialog` route steps): chosen button, cancel, edited text and cancel all return as expected, layouts inside the safe area. `XamShowMessageBoxUIEx` stays a stub (FH1 imports it; no call seen yet). `pinyon_shift_host_xam_dialogs=false` keeps the ImGui dialogs | M |
| NP-5.3 | **Done** (SDK `9acbcbd`, project `66882bf`): F7, SETTINGS > ACHIEVEMENTS and the title's `XamShowAchievementsUI` (a stub until now) open a host list with gamerscore or LOCKED per achievement and the focused one's description; unlocks show a host toast with the title's XDBF icon; the ImGui overlay and toast are no longer created. Unlocks keep the TOML store. Checked by `fh1-host-features` (toast shown, list opened and closed by the provider and by F7). No unlock sound | S |
| NP-5.4 | **Done** (SDK `90285fd`, `a153034`): FH1 reads the console language and country through `ExGetXConfigSetting` (`user_language`, `user_country`) and mounts all 22 `stringtables` archives, loading the `.str` tables of one. A title-screen probe of 22 pairs (`.local/np54/probe.sh`) found the pair for each of the 20 player tables: EN (1,103), GB (1,35), FR (4,34), DE (3,24), IT (6,50), ES (5,31), MX (5,71), BR (9,13), NL (16,74), DA (1,25), NB (15,75), SV (13,90), FI (1,32), PL (11,82), CZ (1,23), HU (1,42), RU (12,88), JP (2,53), KO (7,56) and CHT (8,101); PROFILE > LANGUAGE offers them (restart). `XGetLanguage` now returns `user_language` and the language enum covers 13-17. Japanese, Korean and Chinese text use a vertex shader the English preparation never sees and that was not even in the analysis catalog, so it failed without being recorded; such shaders are now recorded as misses, so one session in those languages feeds the next preparation | M |
| NP-5.5 | **Done** (project `66882bf`): `src/save_backups.cpp` copies the state's `user` directory to `backups/saves/<UTC>` at each session start (when it differs from the newest backup) and after every save once its files have been still for a poll interval, keeping `pinyon_shift_save_backup_slots` (10). PROFILE > SAVE BACKUPS lists them and schedules a restore, which is applied at the next start before the title runs, after the current files are backed up as `before-restore`. Checked on a private state: a session backup of 19 files, a restore scheduled from the settings screen, and a relaunch that applied it with identical file hashes and no staging directories left | S–M |
| NP-5.6 | **Done**: F8 (`bind_photo`) captures the title's image at the internal resolution without host overlays and writes `<state>/photos/pinyon-shift-<UTC>.png` on a background thread (logged as `photo.saved`). The PNG encoder is self-contained (`src/ui/png_writer.cpp`: adaptive None/Sub/Up filters, fixed-Huffman deflate); its output was checked against Python's zlib and CRCs, and a free-roam route pressing F8 wrote the frame | S |

**Gates.** A guest `XamShowMessageBoxUI` shows the host-styled box and
returns the chosen index; `XamShowAchievementsUI` opens the list and returns
after close; booting in German, French and Japanese shows a localised pause
menu with achievement strings following; the gamertag appears where the game
renders it; a restored backup loads and the original file hash is unchanged.

## NP-6 Input

| Item | Work | Size |
| --- | --- | --- |
| NP-6.1 | **Done** (SDK `1167b52`): `pad_remap` maps each physical control (buttons and triggers) to the control the title receives as `PHYSICAL=GAME` pairs, and `pad_invert_right_stick_y` inverts look; CONTROLS > CONTROLLER BUTTONS edits them live. Only the title's reads are remapped: host menus keep the physical layout, so a remap cannot lock the player out of the remap screen. Mouse and keyboard keep their keybinds. `fh1-host-features` with `pad_remap=B=A` reaches free roam pressing only B. Stick-to-stick remapping is not offered | M |
| NP-6.2 | **Done** (NP-1.4): CONTROLS lists the key each pad control maps to in mouse-and-keyboard mode. Keyboard glyphs in the game's own HUD need texture replacement and stay with NP-10.3 | S |
| NP-6.3 | **In part** (SDK `1167b52`): `pad_rumble_strength` (CONTROLS > RUMBLE) scales vibration. Verifying DualSense and Steam Input through SDL and documenting a Deck layout need the hardware (see the human-only list) | S |
| NP-6.4 | **Done** (SDK `e2eb60f`): Controls has MOUSE (off, camera, steering) and MOUSE SENSITIVITY. Steering (`mnk_mouse_steering`) turns horizontal mouse movement into a virtual wheel on the left stick that eases back to centre at `mnk_steering_return` locks per second, with the camera on the right-stick keys | S |

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
| NP-7.1 | **Done** (`c1e0b98`): `frame.tick`, vehicle pose, `save.before_encrypt`, `file.open` and the pause-button site dispatch to subscribed callbacks (free when nothing subscribed); guest tasks drain at `frame.tick` on the main thread through `CallGuestFunction`. The built-in UI experiments still call their code directly: they are test variables, and moving them buys nothing until NP-11.1 | M |
| NP-7.2 | **Done** (`c1e0b98`): `tools/generate-fh1-symbols.py` generates the host table from `config/mod/fh1-symbols.toml` plus every `[[midasm_hook]]` as `hook.<name>`, keyed to the `default.xex` hash; `find_symbol` and `find_offset` read it, and a test fails when the table and the hooks disagree | S |
| NP-7.3 | **Done** (`c1e0b98`): `include/pinyon_mod.h` ABI 1 (append-only, `size`-versioned), loading from `mods/<name>/code/` in `enabled_mods` order after `mod.toml` checks (ABI, `game_version`, `requires`, `load_after`, `conflicts`), never unloaded; guest read and write, `call_guest`, symbols, `subscribe`, guest tasks, cvars, binds, host dialogs and events. `docs/MODDING.md` records the direct-call limitation | M |
| NP-7.4 | **Done** (`c1e0b98`, SDK `8f2d4d4`): an overlay device over the base game device (`ReplaceDevice`, since the VFS resolves the first prefix match) serves loaded mods' `game/` files, earliest mod first, whole files only; each served file logs `mod.file.override` | S |
| NP-7.5 | **Done** (`c1e0b98`, NP-8): with mods or cheats on, the title plays `<state>/user-modded`, copied from the player's profile the first time; every save writes `pinyon_shift_mods.json` with the mods, the active cheats, the mod-set hash and the plaintext body hash. The settings screen shows the modded state on MODS and CHEATS; no schema migration was needed, since `enabled_mods` defaults to empty | S–M |
| NP-7.6 | **Done** (`c1e0b98`, NP-11): `hello_telemetry` (setting, bind, hooks, a guest call, a dialog, a HUD label and a menu action) and the asset-only `english_strings`, `tools/install-sample-mod.py`, `docs/MODDING.md` and the `fh1-mods` and `fh1-mods-ui` routes | S |

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
| NP-8.1 | **Partly done**: `pinyon_shift_cheats` (restart; from the config file or the command line) and the hot `cheat_time_scale` (0.25 to 2, clamped) in a Cheats category; the scale multiplies `f31` in the delta hook before the store at `0x823EDB84`. Measured on free roam: simulation-to-wall ratio 0.503 at 0.5 and 2.010 at 2.0 (1.0 is 0.95 to 1.08). Freeze and teleport are not possible on `0x82BC5A3C`: it is the presentation transform, rewritten from the physics body each frame, so they wait on NP-8.4. The career-intro skip is still to do: the checkpoint seeding it would reuse (`PINYON_SHIFT_M5_TEST_CAREER_CHECKPOINT`) edits the outgoing save and only matches the old 19,472-byte profile, so it belongs on the load-time editor of NP-8.3 and needs a fresh-profile route to test. The title's switches are more than the three named here: `sub_824F8150` reads one token per call (`name` flips a switch, `name=value` sets it, through `sub_82C096E0` and `sub_82C09468`) and knows `forceEnableDebugMenus`, `debugmenudescriptions`, `timeofdayindex`, `highrescubemap`, `frontend30fps`, `mainthread30fps`, `framesperrender`, `maxaicars` and about 60 more (names at `0x82010A2C`-`0x82010D84`); `loadcmdlinedottxt=<path>` and `ignorecmdlinedottxt` (`sub_824E4228`) name a file of more tokens, `GAME:\cmdline.txt` by default. The retail title reads no token source by default: the kernel command line (`cl`) is not imported, and `cmdline.txt` served by a mod is never opened. The parameter object is a lazily built singleton (`sub_82479E88`, pointer at `0x832E2680`) read on demand; feeding `sub_824F8150` host tokens at the first frame tick was tried (`timeofdayindex=3`, `forceEnableDebugMenus`) and changed nothing visible, so each switch's consumer has to be traced before any is offered as a cheat | S |
| NP-8.2 | **Done**: F10 (`bind_trainer`) opens TRAINER on the host layer with PLAYER (set credits, NP-8.3), WORLD (game speed), VEHICLE (freeze and teleport shown disabled until NP-8.4), GRAPHICS (motion blur, depth of field, trilinear) and DEBUG (file-open log); pad, keyboard and mouse navigation; the title pauses into its own pause menu while it is open. Each change logs `cheat.changed`. Cheats stay off until the title plays the isolated `user-modded` profile, and SETTINGS > CHEATS turns them on. `fh1-trainer` drives it | M |
| NP-8.3 | **Done**: the profile body opens with a self-describing section (field count, then `[len][name][0x20][0][type][value]`, structs nested) holding `Main/Credits` (UInt32), `Main/XP`, `Main/Level` and `Main/WristbandLevel`; `tools/fh1-profile.py` decodes, edits and round-trips every captured profile byte-identically, keeping the class-serialised tail raw. The running title keeps money encoded in memory (a scan of the guest heaps finds no plain copy), so an edit to an outgoing save would last one save; the editor instead writes the body the title has just decrypted (a new hook at `0x82C66594` in `sub_82C66308`, also the mods' `save.after_decrypt`). TRAINER > PLAYER > SET CREDITS applies once at the next load and then clears itself. Buy-car from the seed with 1,000,000 set: the showroom shows "Available: 1,000,000 Cr", and the saves after the 120,000 purchase hold 880,000 | M |
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
| NP-9.0 | Split `Fh1NativeExecutor` into an API-agnostic core (surface keys, tile ownership, `ClaimTiles`, `PlanCopy`, `GetResolveSources`, the overwrite-rect interpreter, stats and skips) and a thin D3D12 surface and pipeline layer, validated by byte-identical replays.. **Started** (SDK `8aeca55`): tile ownership (owners, stencil state, claim cache, generation) is the header-only `rex::graphics::Fh1EdramTiles`, whose claims return what the backend must transfer; `pinyon_shift_fh1_edram_tiles_tests` covers it and the golden replays stay byte-identical. `GetResolveSources` splits through its `SplitByOwner`, and surface keys, pitch and height rules are `Fh1SurfaceKey` in `fh1_edram_surfaces.h`. `PlanCopy` and the depth-overwrite rectangles are next | M |
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
| NP-10.1 | **Done** for archive members (the UI scene stream adapter is not needed for it): `tools/build-mod-archives.py` rebuilds each archive a mod changes from the player's copy, copying untouched members' headers and LZX bytes, storing replacements (method 0), updating each record's data-offset extra field (id `0x1123`, the absolute offset of the member's data, which the title reads) and the archive's `zipmanifest.xml` line (`dirsize` includes the end record), as the generated mod `zz-archive-patches`; `launch-preview.ps1` runs it. An unchanged rebuild reproduces 120 of 120 stock archives byte for byte; the one archive whose end record disagrees with the manifest (the 230,057-entry track `bin.zip`) is refused. The title checks a verified file's size against its table in `sub_82C03E90`, so a hook at `0x82C041A4` accepts the real size for files a mod replaces (with the block-hash hooks of NP-10.2). `fh1-pause` with a mod replacing only `EN.zip/PauseMenu.str`: QUIT reads EXIT, no dirty-disc error | M–L |
| NP-10.2 | **Partly done** (`9e87011`): the title opens `media/db/gamedb.slt` loose, so no repacker is needed; `tools/build-mod-patches.py` applies enabled mods' `db/*.sql` in load order to a copy of the player's database, served as the generated first-priority mod `zz-db-patches` (removed when no script is left), run by `launch-preview.ps1` before each start. The title reads the file through a block reader that checks each block against a SHA-256 table for the original (`sub_82BFFDA0` from `sub_82C05530`) and raised the dirty-disc error; two hooks on the check's result accept a mismatch only for files a loaded mod replaces. Verified: a mod setting the Jaguar XKR-S (`Data_Car` 1496) to 1,000 shows "1,000 Cr or 1 Token" in the showroom; without it, 120,000. Merging `physics.zip` and `gametunablesettings.zip` by key waits on NP-10.1 | M |
| NP-10.3 | **Partly done** (SDK `d19ff4a`, `a13808b`): the D3D12 texture cache's CPU load hook dumps DXT1, DXT3 and DXT5 2D textures as `<hash>.dds` (`texture_dump_dir`; XXH3 of the guest base level, untiled on the CPU with every level guest memory holds) and uploads a `<hash>.dds` of the same size and format with at least as many levels instead of the guest data (`texture_replacement_dirs`); loaded mods' `textures/` folders are passed to it in load order. Free roam: 664 dumps (568 DXT1, 96 DXT5) decode correctly, and recolouring all of them shows 634 replacements in the captures (road, terrain, trees, lamps, HUD); a mod folder works the same way. Still to do: other formats, higher-resolution replacements (a resource of their own) and a hot rescan | L |
| NP-10.4 | Optional: Lua 5.4 bound to the mod ABI (symbols, hook points, cvars, guest queue) loaded from `mods/<name>/code/*.lua`, with a per-tick overhead budget. | L |

**Gates.** One tunable XML member and one `.bgf` overridden without whole
archive replacement; a modified `Data_Car` row visible in-game and stock
restored when disabled; one car card texture replaced and rendered correctly
at 1080p and ultrawide.

## NP-11 UI extension API, production

| Item | Work | Size |
| --- | --- | --- |
| NP-11.1 | **Partly done**: the host-layer backend for additive widgets is in the ABI: `set_hud_text` labels drawn over the title in its fonts (1280x720 layout, safe area) and `add_menu_action` rows in SETTINGS > MOD ACTIONS, logged as `mod.menu_action`; `fh1-mods-ui` drives both. The semantic component registry and scene lifecycle of the UI API plan wait on NP-11.3 for anything that edits the title's own scenes | M |
| NP-11.2 | **Done**: string overrides keyed by table and 16-bit entry key (`pinyon_shift::ui::SetUiString`, the mods' `set_ui_string`). A hook on the LSB2 reader's allocation (`0x82CAC704` in `sub_82CAC5B8`) gives tables with overrides room after the pool; the chunk hook writes a replacement in place when it fits, otherwise appends it, points the entry at it and moves the sentinel. The pause SETTINGS label now uses it. `tools/fh1-strings.py` lists a table's keys and text. `fh1-pause` with `hello_telemetry`: PHOTO MODE (10 characters) reads "PHOTO MODE (F8 SAVES A PNG)" (27, appended) and the row art stretches to fit; SETTINGS still replaces MULTIPLAYER | M |
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

- **CI compiles C++.** **Windows done**: `PINYON_SHIFT_HOST_TESTS_ONLY`
  configures without generated game code, and `tools/ci-host-tests.ps1` builds
  `pinyon_shift_host_tests` (the SDK runtime, the UI API, host UI, host config
  and profile-body tests, the shader-pack test and the sample mod) and runs
  the tests that need no game data; the `host-tests` CI job runs it after
  provisioning the pinned toolchain. Locally: all pass. The pass-tracker and
  execution-key tests went with NP-0.5; Linux follows NP-12.1. The job's
  first run on GitHub happens with the next push.
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
- **Known gap in the preparation route.** On `fh1-race-sync` from the
  `appdata-2026-09-27` seed, 2 of 11 runs on 2026-09-28 missed vertex
  shaders `953C0C0D7A505911/7F` and `DAB93405F7249276/0` while the title
  saved a car card, which fails the route's forbidden-error check. The
  preparation route does not render that thumbnail path; until misses
  compile on the fly, the capture route should reach it (or the seed's
  cards should be in a state that renders them every run).

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
