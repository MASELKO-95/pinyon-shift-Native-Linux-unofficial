# Rayman Origins native renderer: lessons for FH1

Reviewed 2026-09-25. Local reference: `.local/RaymanOriginsRecomp`, checked out
at `0046cc11fbc95a5d3942c52c0130236f2065df9f`. This is a source and public
history review, not a reproduced performance benchmark. No Rayman game assets
were supplied or run. The clone is ignored by the FH1 repository.

## Finding

Rayman has a useful architectural advantage beyond being a smaller game:
it translates an ordered stream of game draw calls, including UI, using the
original game shaders. It does not reconstruct semantic scene families and
then recover the HUD from an emulated frame. It reached a complete visible
frame early, then broadened support.

FH1 should borrow that delivery order and evaluate the same kind of coherent
frame boundary. A direct transplant is not justified: our earlier wrapper
census found that seemingly promising draw hooks covered EDRAM copies, while
geometry also arrives through indirect and deferred command paths. A small
coverage experiment should decide whether a D3D-level stream is viable here.
If it is not, reuse the existing command-consumption observations rather than
spending another project rediscovering every D3D entry point.

## What actually runs

1. **Intercept seven graphics entry points plus Present.** Two shader creation
   hooks identify shader containers, three draw hooks collect draws, and Clear
   and Resolve hooks record target operations. The draw hooks read the game's
   D3D device register shadow at the call site. They still call the original
   guest functions, even in native mode.
2. **Copy transient inputs immediately.** `Renderer::Draw` copies vertices,
   indices and constants before guest memory can be reused. It records an
   ordered frame operation rather than waiting to join a title scene with a
   later GPU frame. Texture handling has its own cache and freshness heuristic.
3. **Run original vertex and pixel shaders.** Shader containers are converted
   by XenosRecomp and compiled by DXC to SPIR-V ahead of time. Reflection and a
   small table of measured UbiArt layouts provide vertex attributes. The
   implementation uses Vulkan directly; the older research plan's proposed
   plume/Metal abstraction is not what shipped.
4. **Own the whole output.** Draw, clear and resolve operations execute in
   order into native color targets. The chosen main target is blitted to the
   window. Fonts, menus and HUD are ordinary draws in that stream, so no
   separate HUD mask or pre-UI injection seam is required.
5. **Turn off backend rendering while retaining the guest protocol.** Their
   null ReXGlue backend keeps the generic command processor, but its draw,
   copy and swap implementations do no rendering. A second patch prevents
   ReXGlue from presenting to the native renderer's window. This removes
   Xenos backend graphics work, not all guest D3D or command-processing work.

Primary source: [draw hooks][capture], [renderer][renderer],
[Present integration][hooks], [null backend][null], [window ownership][window].

## Why progress was fast

Their public history shows the following sequence on September 25. Times are
the recorded commit author times, UTC-04:00; they do not establish total
engineering effort, earlier private work, or independent validation.

| Time | Commit | Visible result |
| --- | --- | --- |
| 09:06 | `f5e92de` | D3D/UbiArt map and native plan |
| 10:53 | `c180fef` | Baseline and all 31 packaged shaders compiled |
| 11:08 | `9379244` | Runtime draw/shader capture |
| 11:25 | `76aeb8c` | Captured-frame software reconstruction and textures |
| 11:33–11:44 | `183c29c` → `98f614b` | Offline Vulkan replay, then live shadow window; reported 154/154 captured draws |
| 11:55–12:02 | `bb0ebed` → `375e239` | Null GPU backend and native game window |
| 12:40 | `d5ed427` | Uniform-buffer shader adaptation and reported Android 60 FPS |
| 13:07–13:16 | `83d1f31` → `0f1d605` | Movies, frame limiter and widescreen |
| 15:23–15:32 | `02e7b49` → `ebc864c` | More draw forms, surface recovery, native targets/clears/resolves |

The meaningful pattern is **capture → replay a recognizable complete frame →
live shadow → own presentation → close visible feature gaps**. Their first
live version preceded the generalized clear/resolve implementation. They
did not wait for an all-level parity program before showing native gameplay.
See the [first live shadow change][shadow] and [native-window change][main].

The workload helped substantially. Their documented packaged set is 31
shaders, and one captured gameplay frame contained 154 D3D draws. A small
number of sprite/font/patch layouts covers much of UbiArt's 2D/2.5D rendering.
FH1 has thousands of observed backend draws, depth, multiple views, tiled
passes, vehicles and streamed world resources. Those counts are from
different layers and cannot be treated as a direct performance ratio.

There is also a process lesson for us: FH1 accumulated much more ownership,
fixture and parity infrastructure before the complete user-visible path
worked. That evidence is useful for difficult producers, but another proof
or diagnostic should not count as the next renderer milestone. A complete,
readable moving frame should.

## Their progress and limits

The current [renderer notes][notes] report correct title/menu/world-map/first
level rendering, movies, Android surface recovery, approximately 59 FPS on
M1 and 60 FPS on a Galaxy S23. They explicitly leave later-level AfterFx and
water/refraction validation open. These are the project's reported results;
we have not reproduced them.

The source also contains deliberate simplifications that explain its small
implementation surface:

| Observed implementation | Consequence for FH1 |
| --- | --- |
| One frame in flight; `EndFrame` waits for its fence | Excellent bring-up simplification; measure its cost before adopting it for a large race workload |
| Six fixed vertex strides, stream 0 and reflected inputs | UbiArt-specific; FH1 needs its actual multi-stream and specialized fetch layouts |
| Single color attachment, no depth/stencil pipeline state, culling disabled | Not a usable replacement for FH1's world pipeline |
| Main target selected by largest pitch; full-target scissor | A working title-specific heuristic, not enough to establish FH1's presentation target or viewport semantics |
| Unknown shaders/layouts/draws can be skipped while the frame still presents | Helps incremental visual bring-up, but supplies no whole-frame correctness/fallback guarantee |
| Texture cache samples 32 small spans to detect changes; 1,024-entry budget | This is not exact resource-generation tracking; do not replace FH1's established lifetime checks with it |
| Color resolves are mirrored to host textures; null backend does no copies | Does not establish correct FH1 CPU-visible resolve, query or memexport results |
| Shader bytecode is precompiled, pipelines are created lazily and cached | Native does not automatically mean all pipeline compilation stalls are eliminated |

The old `PROGRESS.md` and portions of `D3D_MAP.md` lag behind the code: they
still describe shadow mode or locating Clear/Resolve as future work. Prefer
the pinned implementation and latest renderer notes when following it.

## Recommended FH1 milestones

This was the original proposal. The active plan built on this study is the
[Xenos retirement backlog](XENOS_RETIREMENT_BACKLOG.md). Keep the existing
D3D12 device, shader pack, route runner and compatibility mode. Do not add a
Vulkan backend or general RHI as part of this work.

| Order | Concrete deliverable | Completion check |
| --- | --- | --- |
| 1. Prove a coherent frame source | Reuse one moving-race capture and one HUD-gap capture to account for ordered draws, clear/resolve operations, target identity and final output. Compare existing title hooks with actual command consumption; explicitly include indirect/deferred work. | Identify one authoritative frame stream and its uncovered paths. If the few D3D wrappers miss geometry, use the existing decoded-command observation boundary for the pilot and record its remaining Xenos dependencies. |
| 2. Render native UI through that stream | Replay the current frame's UI/font draws with their actual shaders, blend/scissor state and target order on a native layer; compose that layer with the existing native scene. | Readable race cues in moving, dense, pause/resume and toggle routes. No fixed rectangular world-copy masks or arbitrary previous-scene reuse. If a UI layer is intentionally retained, prove its lifetime/invalidation from the producer. |
| 3. Make one complete recognizable race frame | Apply original vertex **and pixel** programs to the currently flat world/car materials, using the existing compiled shader infrastructure. Cover depth, target dependencies and alpha needed for that route. | A reviewer can drive the scripted segment with coherent road, vehicles, foliage and HUD. Missing optional effects are listed rather than made blockers. |
| 4. Own presentation continuously | Feed the selected ordered frame directly to native output with a simple, bounded resource lifetime. Keep all frame data together rather than joining independent title/GPU clocks wherever avoidable. | Sustained moving race and mode transitions have no scene/HUD alternation; the whole-frame compatibility escape remains available. |
| 5. Remove duplicate rendering | Suppress only replaced visual work after its outputs and guest-visible consumers are accounted for. Keep required producer work until an equivalent native implementation exists. | A trace proves the replaced visual work no longer runs twice, with working queries/resolves/readbacks and unchanged route behavior. Optimize and assess the deferred performance/visual targets after this. |

The most immediately useful Rayman idea is **UI and presentation as part of
the same ordered renderer**, followed by original pixel shaders. It does not
justify discarding the working FH1 native world feed or installing a null
backend before we cover FH1's GPU-produced dependencies.

FH1 evidence informing this recommendation: [draw census](RENDER_PASS_CENSUS.md),
[guest-visible dependencies](GUEST_VISIBLE_RENDER_DEPENDENCIES.md) and the
[research reference](RESEARCH.md), which also summarizes the retired
scene-native backlog and Skate milestone study.
In particular, the historical census recorded 132,568 exact matches from the
candidate title draw wrappers as EDRAM copies, not the hoped-for prepared
geometry path. That prevents assuming Rayman's three draw hooks cover FH1.

## Code-level details (re-reviewed 2026-09-27)

A second read of the pinned source clarified what the renderer handles and
what it can omit only because of the game it serves:

- The hooks record and then **still call** the original D3D functions, so
  the game keeps writing PM4 and the null backend keeps consuming it. The
  renderer reads the device's register shadow at each draw and resolves the
  draw immediately (vertex copy, index conversion, constants, texture
  decode, pipeline); `EndFrame` replays the operations into one command
  buffer and waits on its fence each frame.
- Targets are keyed by EDRAM base, format and pitch but are always RGBA8,
  single-sample, with no depth/stencil, scissor, alpha test or bool/loop
  constants. Resolves are blits whose destinations permanently replace later
  fetches of that base; nothing is written back to guest memory.
- Textures decode on the CPU (top mip, a handful of formats) and change
  detection hashes 32 sampled spans; unsupported textures fall back to the
  first registered one. Unknown shaders, layouts or primitives skip the draw
  with a logged reason.
- The null backend overrides six command-processor functions to do no
  rendering. Fences, waits, interrupts, swap counting and vblank continue in
  the base class; occlusion queries get the base fake count; memexport
  never runs.
- The whole renderer took about five hours on 2026-09-25: capture, a frame
  dump with software reconstruction, offline replay, a live shadow window,
  the null backend and native window, then movies, widescreen, and finally
  render targets, clears and resolves.

FH1 cannot take these shortcuts: its scene depends on depth/stencil, 4×
MSAA float targets in EDRAM bands, resolve chains with history and real
query counts. The [retirement backlog](XENOS_RETIREMENT_BACKLOG.md#target-architecture)
maps each shortcut to the FH1 requirement that replaces it.

## Reuse boundary

The reference repository has a [GPL-3.0 license][license]; this repository's
root license is BSD-3-Clause. This review adopts architectural observations
and does not copy implementation code. Any future source reuse needs an
explicit licensing decision. Existing shader/runtime components retain their
own licenses.

[notes]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/docs/NATIVE_RENDERER.md
[capture]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/rex/src/native_capture.cpp
[renderer]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/tools/native_renderer/vk_renderer.h
[hooks]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/rex/src/hooks.cpp
[null]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/android/rexglue-patches/0003-rexgpu-null-plugin-command-processing-without-GPU-em.patch
[window]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/android/rexglue-patches/0004-rexgpu-null-no-presenter-leave-the-window-to-the-nat.patch
[shadow]: https://github.com/BelmanteGu/RaymanOriginsRecomp/commit/98f614b
[main]: https://github.com/BelmanteGu/RaymanOriginsRecomp/commit/375e239
[license]: https://github.com/BelmanteGu/RaymanOriginsRecomp/blob/0046cc11fbc95a5d3942c52c0130236f2065df9f/LICENSE
