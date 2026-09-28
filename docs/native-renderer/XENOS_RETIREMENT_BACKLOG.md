# Native renderer: Xenos retirement backlog

Status: **active delivery plan**, created 2026-09-27 at `dev` checkpoint
`02dfad0`. This is the only renderer roadmap. It supersedes the Rayman
complete-frame backlog, the scene-native (SNR) backlog, the performance
backlog and the migration checklist's B/C items; those files are archived
(see [retired plans and evidence](#retired-plans-and-evidence)). The
[Rayman study](RAYMAN_NATIVE_RENDERER_RESEARCH_2026-09-25.md) is the
reference architecture.

## Goal

Replace the Xenos-emulating D3D12 backend with an FH1 native renderer that
draws **every mode of the game** — boot, movies, menus, garage, free roam,
races, map, photo mode and pause — and then remove the Xenos backend from
the runtime build.

"Xenos retired" means all of the following hold:

1. In the shipping configuration every consumed guest draw, clear, resolve
   and swap is executed by the native renderer. The Xenia-derived render
   target cache (EDRAM emulation), texture cache and draw-time state
   translation are neither constructed nor invoked; counters prove it.
2. The native renderer is FH1-specific but **content-generic**: it derives
   work from decoded guest state and the [frame contract](#xr-01--map-the-whole-game-frame-contract),
   never from shader-hash allowlists, guest vtable checks or route shapes.
3. The route matrix below passes against the Xenos reference captures, with
   frame time no worse than Xenos at 1x.
4. A clean runtime build has no Xenos backend linked, and a rollback release
   with Xenos exists.

These pieces are **not** Xenos emulation and stay: the base PM4 command
processor (register file, fences, waits, interrupts, swap counting), the
guest-memory GPU mirror that translated shaders fetch vertices from, the
shader pack with its analysis/pipeline catalogs and offline producer, the
presenter, and the deferred command list and submission code.

## Where we are (analysis, 2026-09-27)

### What works

- **Opt-in native race output** on the Recaro Rush route:
  `--pinyon_shift_native_race=true --pinyon_shift_native_ui_live=true`.
  Continuous promotion over 1,920 scripted frames, readable HUD, hot toggle,
  whole-frame fallback, and correct pause/free-roam/title hand-back.
- **Original shaders.** All captured families use original vertex shaders;
  some use original pixel programs. The installed 1x pack has 24,763
  entries; runtime translation is structurally impossible (strict pack
  gate). The last documented catalogs hold 11,628 analyzed shaders, 461
  pipeline hashes, 73,781 draw identities and 690 copy identities. See
  [shader pack](SHADER_PACK_FORMAT.md).
- **Ordered consumed-command capture** of draws, optimized clears and copies
  with one ordinal, a rolling 64-frame probe, a selected-frame replay
  fixture and an ordered UI capture.
- **Retained Xenos-side changes:** owned 1x depth clear, reflection cube
  mips and direct cube import, the Carson geometry cache, one submission per
  frame and deadline vblank (see [development findings](../DEVELOPMENT.md#retained-changes)).
- **Automation:** render-test scripts, PPM captures, verifiers and perf CSVs
  ([render tests](FH1_RENDER_TEST_AUTOMATION.md)).

### Why the current native path cannot retire Xenos

1. **It runs after Xenos, not instead of it.** Xenos renders every frame
   in full. The native path observes Xenos's prepared/final draws inside
   `D3D12CommandProcessor::IssueDraw`, copies CPU snapshots, serializes six
   family fixtures, re-parses them at swap and redraws. Median race frame:
   **91–95 ms native vs 23.5 ms Xenos** — 42 ms guest draw issue with
   capture, 11 ms snapshots, 8 ms observer, 34 ms native output (parse
   5–7, re-preparation 5–6, per-frame resource creation 4.5–5.6 ms).
2. **It consumes Xenos-produced resources.** Textures are pinned copies of
   the Xenos texture cache (96 MiB / 256 textures outside trace frames);
   the initial color/depth snapshot comes from a Xenos resolve; the front
   buffer comes from Xenos. Native output cannot exist without Xenos.
3. **It is fixture-bound.** The live scene needs six semantic families fed
   by 153 guest hooks (about 35 on the live path), `view == 8` gates, 44
   shader hashes in the app and 66 in the SDK, 22 guest addresses, Recaro
   HUD mask rectangles and hard-coded ordered shapes (destination
   `0x1CE2D000`, 12/15/18 producers, a 640×360 clear). On 2026-09-27 the
   current save reached a point-to-point event instead: the vegetation probe
   found 0 items, **every frame fell back to Xenos while capture kept
   running — about 5 FPS against 44 FPS without native flags**.
4. **Per-frame admission makes double rendering permanent.** A frame can
   only be replaced if Xenos also rendered it, so suppression never became
   safe ("Gate B" stayed closed).
5. **Coverage is a subset.** In the census frame 1,377 of 2,972 draws are
   unsupported; about 95 copies and 20 clears per frame are counted but
   skipped. No MSAA, stencil or general blending; targets are created per
   frame; 1280×720 is hard-coded about 105 times. Visible result: flat sky,
   unlit world, dark car. Three large indexed families are kept out after
   `DEVICE_HUNG` or backend failures.
6. **No tests cover the native C++.** `graphics_hooks`, `guest_output_renderer`,
   `native_output_track`, `native_output_ui` and `ordered_ui_capture` have no
   automated test; the 83 Python `test_native_renderer_*` files test log tools.

Conclusion: the six-family pilot proved original shaders, admission,
presentation hand-over and tooling. It is a **dead end for retirement**;
continuing to add families or pins cannot remove Xenos. Freeze it.

### What carries forward

- SDK seam positions: `IssueDraw` after state is final, `IssueCopy`, the
  optimized-clear report, `IssueSwap`'s `kNativeAttempt` refresher branch
  that skips Xenos gamma/FXAA, and the deferred command list.
- The base command processor already produces every PM4 side effect a null
  backend needs: RPTR/scratch writeback, `WAIT_REG_MEM`, `MEM_WRITE`,
  `EVENT_WRITE*`, interrupts, `XE_SWAP` counting, vblank and fake
  query/ZPD counts (`thirdparty/shiftglue-sdk/src/graphics/command_processor.cpp`).
- The ordered Frame/Event model, the ordered producer executor (closest code
  to a generic executor), texture identity (`allocation_id` +
  `payload_generation`), multi-stream vertex and index conversion from the
  remainder parser, the hot toggle and promotion plumbing.
- The shader pack, catalogs and producer, the render-test runner and
  verifiers, and the [frame facts](#fh1-frame-facts-carried-forward).

## Target architecture

The [Rayman renderer](RAYMAN_NATIVE_RENDERER_RESEARCH_2026-09-25.md) records
an ordered list of draw/clear/resolve operations while the game runs, draws
them with the original shaders into native targets, presents natively and
runs ReXGlue with a null backend that still consumes PM4. UI, fonts and
movies are ordinary draws in that stream. It reached a complete frame in
hours because it could ignore depth, MSAA, formats and guest write-back.
FH1 keeps the structure and replaces those shortcuts:

| Rayman | FH1 native renderer |
| --- | --- |
| Hooks title D3D calls | Consumes decoded PM4 at `IssueDraw`/`IssueCopy`: FH1 draws arrive through indirect command buffers, and title wrappers matched only EDRAM copies |
| Records ops, replays at Present | Executes each op **inline in guest order** on the GPU thread; live inputs, no snapshots or fixtures |
| RGBA8 targets, no depth/MSAA | Surfaces keyed by EDRAM configuration with real formats, 4× MSAA, depth/stencil and band mapping |
| Resolve = blit, never written back | Resolves into native textures keyed by guest range + generation; later fetches bind them |
| Vertices copied per draw | Vertex fetch from the guest-memory GPU mirror, the ABI the pack shaders expect |
| Texture change = 32 sampled spans | Generation tracking from guest-memory write watches |
| Unknown draws skipped silently | Skipped **with a reason**, counted and failing tests where coverage is claimed |
| Fake query counts | Real occlusion counts or a per-consumer proof |

Components (new code lives in the SDK's FH1 D3D12 plugin, next to the pack
loader, so per-draw work never crosses the plugin ABI):

```text
PM4 command processor (kept)
  └─ IssueDraw / IssueCopy / clears / IssueSwap
       ├─ xenos  : existing D3D12CommandProcessor path (reference, rollback)
       └─ native : FH1 native executor
             state decode ── shared helpers extracted from the Xenos path
             │                (system constants, viewport, primitive processing)
             pipelines ───── pack DXIL + binding metadata, PSOs from catalog
             surfaces ────── EDRAM replacement: formats, MSAA, bands, aliases
             resolves ────── native textures keyed by guest range/generation
             textures ────── guest-memory textures: untile/decode, generations
             geometry ────── guest-memory GPU mirror (vfetch/memexport ABI)
             presentation ── native front buffer → gamma/FXAA → presenter
```

**Session modes** (restart-level setting, proposed
`pinyon_shift_renderer = xenos | native-shadow | native`):

- `xenos` — today's renderer; stays the default until XR-10.
- `native-shadow` — both run on the same frame; Xenos presents, native
  renders privately. Test frames read back both: an exact same-frame pair,
  free of the game-time drift that spoiled separate-run comparisons.
- `native` — native presents every frame; after XR-07 Xenos rendering is
  off (null). No per-frame switching between renderers in this mode.

The executor is also built as a library for an **offline replayer** of
locally dumped frames (Rayman's dump/offline replay loop): seconds per
iteration and deterministic regression tests. Dumps contain game data and
stay under `.local`.

**Where performance should come from.** On Xenos the title thread's top CPU
hotspots (`sub_829F04A8`, `sub_823E91F0`: 7.87 and 5.43 ms per race frame)
busy-poll a word that the command processor writes with `EVENT_WRITE_SHD`
while consuming PM4 — the game waits for the GPU command thread. A leaner
native draw path shortens that thread directly. Smaller candidates:
resolves that skip the guest-memory layout and texture reload (measured
small for one post-chain surface, about 0.07 ms of GPU time per frame),
no EDRAM ownership transfers, and occlusion counts without a blocking fence
wait. None of this is measured for a native path yet; the XR-03 go/no-go
and XR-07 decide.

## Rules

- **Complete frames first, then broaden.** Bring up the simplest complete
  frames (boot, movie, menus) before the race.
- **Content-generic only.** No shader-hash allowlists, family builders,
  guest vtable/address checks, view ordinals or route-specific shapes in the
  native renderer. Title knowledge enters only through the frame contract,
  and each contract entry must hold across the whole route matrix.
- **Skip with a reason.** Every unsupported event increments a named
  counter and is logged in a bounded summary. A mode is "covered" only when
  its skip counters are zero; tests enforce that.
- **Judge final frames.** Use same-frame `native-shadow` pairs. Intermediate
  comparisons locate causes; MAE is supporting evidence, not a gate. A
  dark-matching intermediate once made the final race worse. Take guest
  references only from runs where native did not present: a promoted native
  image once reappeared in a reused guest output buffer three frames later.
- **Freeze identities per comparison:** source, SDK, binary, settings, pack
  and catalogs. Report median/p95/p99, CPU/GPU time, memory and skips from
  repeated matched controls; keep profiling and readback outside timing
  windows. Missing geometry, flicker, broken transparency or timing errors
  fail a change regardless of averages.
- **Two inconclusive trials on one gap:** record it, re-rank or change the
  seam. Isolate any GPU fault with DRED and the debug layer before retrying.
- **Measure from the first build.** Report executor CPU/GPU per frame with
  every milestone; `native-shadow` overhead is a tracked number.
- **Delete superseded code in the same milestone** that supersedes it.
- **Test the executor offline** (dump replay) and the product by routes.
- **Do not move, reset or overwrite saves** for tests; use disposable seeds
  ([AGENTS.md](../../AGENTS.md)).

## Risks to watch

- **Shared-helper drift.** The pack's DXIL was compiled against the Xenos
  translator's system-constant layout, bindless ABI and host render-target
  format mapping. Helper extraction (XR-02) must leave the Xenos path
  byte-for-byte unchanged; rerun the Xenos route matrix after each step.
- **Command-thread cost.** The executor runs inline on the thread the title
  waits on. In `native-shadow` it adds to Xenos's cost; in `native` it must
  be cheaper than Xenos or frame time rises. Track it from the first build.
- **GPU-written guest memory.** The mirror marks resolve destinations as
  GPU-written. A vertex fetch or memexport input inside such a range would
  read stale data once resolves stop writing the mirror (XR-07); XR-06 must
  prove none exists or bridge it.
- **Unseen content.** Formats, aliases or resolve kinds absent from the
  census (night, weather, photo filters, livery editor) will appear later.
  They must surface as named skips and contract updates, never as silent
  fallback.
- **Route drift.** Save progress and scripted timing change what a route
  reaches. Run from pinned disposable seeds and verify the scene (HUD,
  mode) before trusting a capture.

## Milestones

| ID | Outcome | Depends on | Size |
| --- | --- | --- | --- |
| XR-00 | Deterministic, mode-complete test bed and Xenos baselines | — | S |
| XR-01 | Whole-game frame contract | XR-00 | M |
| XR-02 | Native executor; first complete frames in shadow | XR-01 | L |
| XR-03 | EDRAM replacement: surfaces, clears, resolves | XR-02 | L |
| XR-04 | Native textures: no borrowed Xenos resources | XR-03 | L |
| XR-05 | Native presentation; `native` session mode | XR-04 | M |
| XR-06 | Guest-visible side effects without Xenos | XR-01, XR-05 | M |
| XR-07 | Null Xenos in `native` mode; performance parity | XR-05, XR-06 | M |
| XR-08 | Close visible gaps across the mode matrix | XR-07 | L |
| XR-09 | Scaling, performance and hardware qualification | XR-07 | M |
| XR-10 | Native by default; remove Xenos | XR-08, XR-09 | M |

Sizes are relative scope, not time estimates. XR-08 and XR-09 can overlap.

### XR-00 — Reset the test bed and baselines

- [x] Freeze the six-family race pilot at `02dfad0`: no new families,
  probes, pins or allowlists. Keep it opt-in until XR-05 replaces it.
  Nothing was added to it; the executor gates the Xenos-side families off.
- [ ] Make routes deterministic: run each through
  `tools/run-fh1-render-test.py` with a disposable seed whose event is
  pinned. Save progress already moved the AppData route from Recaro Rush to
  another event, which silently changed what "native race" tested.
  `tools/create-render-seed.py` and the runner's `--configuration` and
  `--hidden` options now exist, and the native race profile passes from seed
  `appdata-2026-09-27`. Remaining: routes still count output frames while
  menus run on wall time (a visible 120 Hz window missed menu inputs), so
  add state-aware waits to the script format (for example, wait for the race
  admission signal or a mode change) before relying on unattended routes.
  Done: `wait <frame> <max> vehicle | vehicle-moved <units> | movie <text>`
  holds the script clock on game state (`fh1-race-start-wait` waits for the
  car to move before capturing); the existing routes still need converting
  where they drift (the car-select and race entries do).
- [ ] Add missing mode routes: boot with movies, Press Start, main and
  single-player menus, garage/car select, a second race event, loading
  screens, rewind. Reuse the free-roam, map, pause and photo scripts.
- [x] Limit `pinyon_shift_skip_opening_movies` (hook at `0x82E5D8AC` in the
  XMedia wrapper `sub_82E5D868`) to `media/ui/videos/splash_intros/`. It
  completed every movie, so scripted runs showed a solid green Press Start
  screen and pink noise behind the single-player menu. A kernel file-open
  observer now tracks the playing movie (`041d541`): the splash intros are
  skipped, `PressStart.wmv` plays, and Press Start is still up by frame 400.
- [x] Bound native diagnostics: no per-draw INFO JSON by default, and stop
  the "absent from the offline analysis catalog" error flood that native
  runs trigger in `PipelineCache::ConfigurePipeline`. Each missing catalog
  shader is reported once, backend draw failures at the first 16 and then
  powers of two; executor trace and log messages are built only on
  verification dump frames.
- [x] Record which renderer presented each capture in the capture event.
  `verify-native-race-mode-boundary.py` and `verify-native-race-toggle.py`
  detect native frames by the pilot's flat sky color (28, 56, 110), which
  stops working once native draws the real sky. Capture events carry
  `presenter` (xenos, pilot, native) and `session_renderer`; the verifiers
  read them with `--events`.
- [ ] Record Xenos baselines per route: median/p95/p99 frame and GPU time,
  draws, copies, clears and VRAM (`tools/summarize-performance.py`). Reuse
  the fixed windows from the performance program: open world wall seconds
  20–46.7 of `fh1-open-world-performance`, Recaro seconds 40–76.5 of
  `fh1-race` (moving from 70), and `fh1-timing-straight` seconds 29–33;
  warmed A/B/B/A blocks; reject regressions above 3% median/p95.

**Done when** every matrix route exits normally on Xenos with valid captures
(including real title video) and a stored baseline.

### XR-01 — Map the whole-game frame contract

The FH1 equivalent of Rayman's D3D map: the finite list of what the native
renderer must implement, measured across all modes instead of guessed from
one race.

- [x] Turn the rolling ordered seam into an always-available,
  metadata-only census for any frame in any mode: bounded ring, counted
  overflow, no payload copies. Per event record:
  - draw: shader pair and modification, catalog pipeline identity,
    primitive/index format, vertex fetch constants, texture fetch constants
    (base, format, size, pitch, tiling, mips, dimension, signedness) and
    whether the base lies in an earlier resolve destination; surface/color/
    depth info, MSAA, window offset, viewport/scissor, blend/depth/stencil/
    raster/color-mask state, bin mask/select, memexport flag;
  - clear: target, rectangles, values; copy: source, destination, format,
    exponent bias, sample select, clear flags, rectangle;
  - query begin/end/ZPD; swap: front buffer address, format and size.
- [x] Add `tools/summarize-native-frame-contract.py`: per-mode tables of
  surface configurations and band layouts, alias pairs, the resolve graph
  (producer → destination → consumers), texture formats/dimensions,
  primitive and index types, vertex formats, shader-pair counts, memexport
  shaders, query use and front-buffer formats.
- [x] Publish `docs/native-renderer/NATIVE_FRAME_CONTRACT.md` with those
  tables and the remaining unknowns. It replaces "families" as the unit of
  coverage.
- [x] Measure census overhead at default log level (target: under 1 ms per
  frame). 172 ns per draw: median 0.41 ms, p95 0.92 ms, worst race window
  1.005 ms per frame.

**Done when** the route matrix is inventoried with zero overflow and zero
unclassified events, and memexport/query use per mode is either counted or
explicitly unobserved.

Status: done for every route that exists (17,580 frames, zero overflow,
memexport zero, ZPD counted per mode; see the
[contract](NATIVE_FRAME_CONTRACT.md)). The second race event and rewind
have no pinned route yet (XR-00) and enter the contract when they do.

### XR-02 — Native executor: first complete frames in shadow

- [x] SDK: add an FH1 native execution path invoked synchronously at the
  consumed seam (`IssueDraw` after state is final, `IssueCopy`, optimized
  clears, `IssueSwap`). Pass pack **binding metadata** (texture/sampler
  bindings, used-texture mask) with bytecode; the app currently hard-codes
  descriptor layouts because only bytecode is exposed. Replace the SDK's
  by-name reads of 16 app cvars with explicit configuration; bump the plugin
  ABI if the contract changes. Done as `Fh1NativeExecutor`
  (`thirdparty/shiftglue-sdk/src/graphics/d3d12/fh1_native_executor.cpp`),
  inside the command processor so no per-draw work crosses the plugin ABI.
  The pack's texture and sampler bindings are loaded with its bytecode and
  drive the bindless descriptor indices. The by-name reads of app cvars
  belong to the frozen pilot and go with it in XR-10. The plugin ABI moved
  to 2 for the presenter field.
- [x] Extract shared state helpers from `D3D12CommandProcessor` (system
  constants, viewport/scissor, primitive processing, blend/depth/stencil
  translation) instead of forking them, so native draws stay on the ABI
  the pack was compiled for. Nothing is forked: in `native` mode the
  command processor's own state code (system constants, viewport, primitive
  processing, pipeline description) prepares every draw and only the
  render-target binding, clears and resolves come from the executor.
- [x] Executor core: root signature per the pack ABI, persistent PSO cache
  keyed by shader pair + modification + state and warmed from the pipeline
  catalog, vertex fetch from the guest-memory GPU mirror exactly as Xenos
  binds it (no per-draw vertex copies, no fetch-address rebasing). The
  pipeline cache and catalog prewarm are shared; the Xenos-side family
  pipelines are off with the executor (prewarm cached failed family
  pipelines and dropped 6.4M draws until `2414d8b`).
- [x] During bring-up only, borrow guest-memory textures from the Xenos
  texture cache at the same draw, counted as `borrowed_xenos_texture`.
  Everything else is native. Never needed: the executor decodes textures
  from its own mirror from the first build (`borrowed_xenos_texture == 0`).
- [x] `native-shadow` mode with same-frame readback on test frames, skip
  counters by reason, a bounded periodic summary, and DRED in debug builds.
  `fh1_renderer=native-shadow` with `fh1_native_shadow_dump_frames` writes
  native and Xenos front buffers of the same swap; `fh1_native_shadow_verify`
  compares every resolve's bytes, the surfaces behind it, EDRAM ownership and
  texture mirrors in the frames before each dump (`..._verify_draws` adds
  per-draw before/after checks); skips and stats print every 600 frames.
  DRED breadcrumbs and page faults come with the D3D12 debug layer.
- [ ] Frame dump (event stream plus referenced guest memory and shader
  identities) and the offline replayer; the first executor regression tests
  run from local dumps.
- [x] First complete frames: legal screen, Press Start with its movie, main
  and single-player menus, a loading screen. All bit-identical in shadow
  mode with zero skips.

**Done when** those frames render in `native-shadow` with zero skips and pass
same-frame comparison, and replay tests run offline.

### XR-03 — Replace EDRAM: surfaces, clears and resolves

- [x] Surface model from the contract: logical surfaces keyed by EDRAM
  base, pitch, format, MSAA and depth format. Map band passes (the race
  scene's 256/256/208-row bands) into one full-size surface through the
  guest's own window offset and viewport, the way Xenos addresses EDRAM —
  not by rewriting NDC or system constants as the pilot does. Bands share
  one surface per key, addressed by the guest's registers; a 2048-tile
  ownership map transfers words between aliased keys.
- [x] Host formats identical to the pack's translation configuration.
  Handle aliases explicitly, e.g. color formats 3 and 12 over one base.
  Includes 64bpp and 16-bit channel formats as raw bits (photo mode).
- [x] Clears (optimized and draw-based), depth/stencil including stencil
  reference, multiple render targets.
- [x] Resolves for every contract kind: color with sample average/select,
  exponent bias and format conversion; depth; partial rectangles;
  clear-after-resolve. Destinations are **persistent native textures** keyed
  by guest range, updated in place by partial resolves, versioned by
  generation, and viewable under every guest format the contract records
  for them (some post-chain addresses are sampled under several formats).
  Fetches inside a destination bind that texture. Changed design: resolves
  write the guest texture layout into the guest-memory mirror, as the guest
  GPU does, and the texture manager decodes it under whatever format a
  fetch names. That handles partial updates, several formats per address
  and CPU reuse with the same page tracking as every other texture, and
  verification compares those bytes with Xenos directly. Remaining
  differences: D24S8 resolves round to nearest where Xenos lands one
  LSB lower, and float24 low mantissa bits.
- [x] Re-test the `DEVICE_HUNG` families under this model with DRED: VS
  `B4995BF113A7CE67` / PS `90CAB86BE8159DA8` (8,700 indices per band),
  the indexed strip `34BA51B282130FF0` / `7D5784B818252517` (up to 14,816
  indices per band), and the sky writer `12BA4E86B158D049` /
  `CAE25D74AD7B16CB` (9,300 indices). The 8,700-index family hung only in
  the third band with the pilot's rewritten viewport (NDC y scale 3.46,
  offset 2.46); the first two bands were safe. The strip family hung even
  with every draw capped at 1,024 indices and a flat pixel shader. DRED
  stopped at a draw with no page fault. Different shaders and sizes failing
  this way point at the pilot's per-draw setup rather than the content.
  Under the executor these shaders are ordinary draws: every route of the
  matrix ran in `native` mode with no device removal.
- [ ] Complete frames in shadow: free roam, Recaro and one other race event,
  garage/car select. Free roam, Recaro, car select, map, pause, photo mode
  and loading screens match (0-0.01% of pixels outside races, about 1.2% in
  race frames). The second race event still has no route.
- [x] **Go/no-go:** compare executor CPU+GPU time per frame with the Xenos
  backend on the same frames. If native is not cheaper, record why before
  starting XR-04. Go: `native` mode on the mode-boundary route has a race
  median of 24.8 ms (Xenos 25.1 ms) and p95 of 31.2 ms (32.4 ms); menus
  11.6 ms (11.1 ms). GPU time is higher (14.7 vs 12.0 ms in races) because
  ownership transfers draw per sample and depth transfers take nine passes;
  frames are CPU-bound, so it is not visible yet (XR-09).

**Done when** shadow race and free-roam frames match Xenos on sky, lighting,
car paint, foliage alpha and HUD across moving frames with zero skips, every
resolve kind runs natively, and no family code is involved.

### XR-04 — Native textures: no borrowed Xenos resources

- [x] Native texture manager for guest-memory textures: key from the fetch
  constant (base, format, dimensions, pitch, tiling, mip range, endian,
  swizzle), generations from guest-memory write tracking, untiling and
  decode by refactoring the existing load shaders into a shared library.
  Cover every contract format: BC/DXT, 8-bit video planes, 16-bit, float,
  signed, gamma, 3D/cube/array and packed mip tails. The texture cache's
  guest-memory decoding is reused as the native texture manager over the
  executor's mirror (in `native` mode, the only mirror); its load shaders
  are the shared library. Texture mirror bytes match CPU memory in
  verification.
- [x] Native resolve outputs take precedence over guest-memory decoding for
  matching ranges. By construction: resolves write the mirror and mark the
  range GPU-written, which invalidates overlapping textures.
- [x] Samplers from fetch constants (filter, anisotropy, clamp, border, LOD
  bias) instead of the pilot's shared approximations. The command
  processor's sampler translation from fetch constants is used as is.
- [ ] Car select card images: render correctly natively; record the Xenos
  cause of the pink stripes (identical in every capture since 2026-09-21).

**Done when** the route matrix runs in `native-shadow` with
`borrowed_xenos_texture == 0` and native texture memory within a stated
budget.

### XR-05 — Native presentation and the `native` mode

- [x] Native swap: take the front buffer from the native surface or resolve
  output instead of untiling guest memory, apply the PWL/table gamma ramp
  (expose it to the native path; it is protected in the command processor
  today), keep FXAA, source presentation and HFR options, and support every
  guest video mode instead of 1280×720 literals. The guest's own swap
  resolve writes the front buffer natively into the mirror, and the command
  processor's swap path (PWL/table gamma, FXAA, source presentation, video
  mode sizes) presents it unchanged.
- [x] `native` session mode presents every frame natively, with no
  per-frame admission or fallback (`fh1_renderer=native`, 1x only).
- [x] Launcher renderer choice (Xenos / Native — experimental) with a config
  schema migration. Config schema 22 (`fh1_renderer`).
- [ ] Remove race admission, the pre-UI hook and the HUD mask blit from the
  product path.

**Done when** one session — boot, title, menus, free roam, two race events,
pause, map, photo mode, back to title — presents natively with correct gamma
and no fallback, and passes the route acceptance against Xenos captures.

### XR-06 — Guest-visible side effects without Xenos

- [ ] Occlusion queries: identify the consumers (the verified lifecycle
  owner is `0x82D951E0`; the race frame also has 24 no-write indirect point
  draws that are likely query probes). Implement native counts written at
  ZPD end without a blocking GPU-thread wait — Xenos `legacy` mode waits on
  a fence today — or prove the fake-count policy is visually equivalent per
  consumer. Keep the ZPD fixes recorded in
  [`EPIC_04`](../../config/rexglue/EPIC_04_ZPD_LIFECYCLE_D3D12.md) and
  [`EPIC_05`](../../config/rexglue/EPIC_05_ZPD_POLICY_GUARD.md).
- [x] Keep PM4 timing semantics: the title polls a word the command
  processor writes with `EVENT_WRITE_SHD` while parsing, so that write must
  stay tied to consumption order in `native` and null modes (the base
  command processor does this today; do not move it to GPU completion).
  Unchanged: the executor never touches the base command processor.
- [x] Memexport: list the shaders with memory export from the analysis
  catalog and where they run; execute them against the guest-memory mirror
  or prove them unused per mode. The census counts zero memexport draws in
  every mode on both renderers; a memexport draw would still run through
  the command processor's path in `native` mode.
- [x] Resolved data read through guest memory: Xenos D3D12 only ever wrote
  resolves to the GPU mirror, never guest RAM. Keep that parity, and prove
  that no GPU consumer reads a native resolve destination through the mirror
  (vertex fetch, memexport input). Parity by construction: native resolves
  write the same mirror bytes (verified against Xenos) with the same page
  state, so any consumer sees what it saw on Xenos.
- [x] Extend the side-effect report to `native` sessions. The frame census
  runs in every renderer mode; on the mode-boundary route `native` matches
  Xenos: zero memexport and query draws, 347,781 vs 346,231 copies, zero
  overflow.

**Done when** the report shows every side-effect class implemented natively
or proven unused on every matrix route, with overflow-safe accounting.

### XR-07 — Null Xenos and performance parity

- [x] In `native` mode construct no Xenos render target cache, texture cache
  or draw-time pipeline path; `IssueDraw`/`IssueCopy` reach only the native
  executor; the base command processor keeps PM4 side effects. The render
  target cache is initialized config-only (no EDRAM buffer, render targets,
  transfers or resolve pipelines), no second mirror or texture cache exists,
  and the pipeline path is the shared pack pipeline cache.
- [ ] Remove observers, CPU snapshots and texture pinning from the `native`
  hot path.
- [ ] Counters prove zero Xenos render-target, texture and pipeline work.
- [ ] Median and p95 frame time in `native` no worse than the XR-00 Xenos
  baselines at 1x on every route; memory within budget.

**Done when** all four hold.

### XR-08 — Close visible gaps across the mode matrix

- [ ] Per mode, rank visible differences from same-frame pairs and fix them
  through the contract: FMV, fonts/UI, garage/car select/livery/thumbnails,
  free roam day/night, traffic, race HUD/minimap, rewind, map, photo mode,
  pause, loading, post chain (bloom, exposure, depth of field, motion blur
  settings), shadows, reflections, particles/skids and streaming.
- [ ] Unscripted drives (race and free roam) in `native`: record control
  response, stability and the first defect that interferes with driving.

**Done when** every matrix mode passes the acceptance rules with an explicit
list of accepted differences.

### XR-09 — Scaling, performance and hardware

- [ ] Native 2x/3x scaling (surfaces and resolves at scale; pack per scale).
- [ ] Band merge: test whether band passes can execute once into the full
  surface without changing the image; retain only with a matched A/B.
- [ ] Startup PSO build from the catalog, upload/descriptor ring reuse and
  barrier batching, each retained only with a measured gain.
- [ ] AMD, Intel and lower-end hardware measurements with stated settings.

**Done when** measured frame-time and memory results are published per scale
and vendor tested.

### XR-10 — Native by default; remove Xenos

- [ ] Make `native` the default for one release with `xenos` as the rollback.
- [ ] Remove from the runtime: the Xenos render target cache, texture cache
  orchestration and draw-time pipeline path; the Xenos-side owned-depth-clear
  and mip replacements (superseded); SDK FH1 hash lists; the six-family
  capture, SNR probes and fixture parsers; ordered UI capture; the pre-UI
  hook; HUD masks; the 153 SNR guest hooks and the race admission hook; the
  obsolete cvars.
- [ ] Clean build with no Xenos backend linked; the offline shader producer
  stays separate; tag the rollback release.

**Done when** that build passes the route matrix and the docs describe only
the native renderer.

## Route matrix

Status reflects `02dfad0`. "Pilot" is the frozen six-family path.

| Mode | Route (existing or XR-00) | Native status |
| --- | --- | --- |
| Boot, legal, splash movies | `fh1-fmv` with movies | Xenos only |
| Press Start and menus | new, movies on | Xenos only |
| Garage / car select | `fh1-race` entry | Xenos only (card images corrupt on Xenos) |
| Free roam | `fh1-free-roam`, `fh1-source-60` | Xenos only |
| Race: Recaro Rush | `fh1-native-race-*` | Pilot, race only, 91–95 ms |
| Race: other event | new, pinned seed | Pilot falls back every frame |
| Pause / rewind | `fh1-pause`, new rewind | Xenos only |
| Map | `fh1-map`, `fh1-moving-map` | Xenos only |
| Photo mode | `fh1-photo-mode` | Xenos only |
| Loading / transitions | `fh1-native-race-mode-boundary` | Xenos only |

## FH1 frame facts carried forward

Measured facts from the archived evidence, for XR-01 to confirm or correct:

- Race frame: 2,900–4,350 consumed draws, about 20 clears and 95 copies;
  48 ordinals are reflection-mip replacement skips that record no draw.
  A RenderDoc race-start frame (5,439 draw actions) ran, in order: 1,079
  draws into a D24S8 depth target, 1,094 into a D32S8 depth target (both
  later read by compute and pixel shaders), 197 into separate 2× MSAA
  color/depth targets, then 2,579 main-scene draws at 4× MSAA.
- Main scene: three EDRAM bands of 256/256/208 rows reusing one 1280×512
  `R16G16B16A16_FLOAT` color and `D32S8` depth host target at 4× MSAA with
  per-band viewport/scissor. Per-band draw lists differ (e.g. 45/67/67 for
  one family); the middle band differs at system word 44. Sky, race line,
  particles and a depth-tested view strip interleave between scene lists in
  every band, so no end-of-frame overlay can replace them.
- Scene color resolves average all four samples (sample mode 6) into
  2:10:10:10 UNORM with exponent bias −2 (`RB_COPY_DEST_INFO=0x003E0382`).
- The final composite reads the 1280×720 R10G10B10A2 scene, a 320×192
  R10G10B10A2 reduction, a 640×360 RGBA8 intermediate with a car-shaped
  alpha mask, and a 16³ color-grading LUT.
- One post-chain destination is temporal: a padded 1280×736 surface
  updated 256 rows per frame, fully republished every other frame, read
  back the next frame, with alternating history destinations
  (`0x1BDB1000`/`0x1C149000`) and several guest formats per address.
- Color formats 3 and 12 (`0x00030000` / `0x000C0000`) alias one EDRAM
  base; isolating either removes complementary content.
- The initial full-size color resolve (guest `484626432`) had 1,274 later
  texture bindings in one frame; the depth resolve to `497831936` had 15.
  Main tile updates are depth resolves; the terminal full-frame copy selects
  color; the scene color copy uses exponent bias −2 and 4-sample mode.
- UI target: surface `0x14000500`, color `0x000A0000` (format 10),
  1280×720, about 166 draws over a few shader pairs. Some frames emit no UI
  pass and keep the previous HUD in the target.
- Post chain: 320×192, 64×32 and 32×32 reductions, a 640×360 producer clear
  and a prior-frame 320×192 feedback read.
- The car's rear overlay is stencil-rejected on Xenos. Matching the guest's
  back-face culling fixed the native route/minimap texture.
- Texture identity is `allocation_id` + `payload_generation`; dynamic
  textures change generation within a frame.
- D3D12 Xenos writes resolves and memexport only to the GPU guest-memory
  mirror. `legacy` occlusion queries return real counts through a blocking
  fence wait.
- `VdSwap` accepts 8888 or 2_10_10_10_AS_16 front buffers; the presenter
  mailbox is R10G10B10A2. The output hook's frame N presents source frame
  N−1; every capture/output join must apply that offset.
- Movies: the Press Start background is `media/ui/videos/PressStart.wmv`
  (the single-player menu showed the same video); boot plays
  `splash_intros/*.wmv`; `FMV_01/02/04.wmv` usage is unverified. The guest
  decodes frames into texture planes, so natively they are ordinary
  textured draws.

## Retired plans and evidence

Superseded plans and dated evidence journals were removed on 2026-09-27;
the [research reference](RESEARCH.md#rayman-and-scene-native-era-2026-09-22-to-2026-09-27)
summarizes what they established. Every file remains in Git at checkpoint
`02dfad0` (the link resolves once `dev` is pushed):
[docs/native-renderer at 02dfad0](https://github.com/arcanite24/pinyon-shift/tree/02dfad07fc1236625520a948bb5cd2afe74bfbb3/docs/native-renderer).
Retrieve one locally with:

```powershell
git show 02dfad0:docs/native-renderer/RAYMAN_STYLE_NATIVE_RENDERER_BACKLOG.md
```
