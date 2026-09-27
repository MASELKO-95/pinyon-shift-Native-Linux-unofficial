# Native renderer: complete-frame backlog

Status: active delivery plan, updated 2026-09-27. This supersedes the delivery
order
in the [scene-native backlog](SCENE_NATIVE_RENDERER_BACKLOG.md); that document
remains the implementation and evidence ledger. The
[Rayman study](RAYMAN_NATIVE_RENDERER_RESEARCH_2026-09-25.md) explains the
reference architecture and its limits for FH1.

## Target and rules

Ship a **usable, opt-in native race renderer quickly**: one continuous moving
race with coherent world, cars and readable HUD. Approximate nonessential
effects are acceptable. Measure frame time once the live image works; fix a
measured responsiveness blocker before spending more time on visual polish.
Menus, free roam and unsupported race states can use the existing compatibility
renderer. A rough or incomplete frame is acceptable in offline/shadow bring-up,
but never replace a visible compatibility frame with stale, blank or
unreadable output. Keep the whole-frame fallback and hot toggle.

Follow Rayman's delivery sequence: **ordered capture → offline replay → live
shadow → native presentation → fill visible gaps → remove duplicate work**.
Reuse FH1's D3D12 backend and current shader/resource infrastructure; this is
not a Vulkan port or a new renderer abstraction. Rayman's GPL source is an
architectural reference, not code to copy into this BSD repository.

## Delivery discipline

The scripted RAY-03 takeover works, but the authoritative ordered replay is
still gated to one selected trace frame. Continuous native race presentation
uses the live scene snapshot. Treat selected-frame passes as diagnostic until
their work runs across moving live frames and improves the displayed result.
Do not equate another captured intermediate, shader binding or passing probe
with progress on RAY-04 or RAY-05.

For each visible gap, capture a final native/compatibility pair from the same
run with frame identity and promotion state recorded. Confirm the guest image
has not inherited a previously promoted native buffer. Rank gaps by effect
on driving and HUD readability, then name one target region before editing.
Use intermediate comparisons to locate the cause; accept a change only after
the final frame improves across multiple moving frames, the HUD stays stable,
and whole-frame fallback still works. Pixel MAE is supporting evidence, not
a quality gate: matching a dark guest intermediate made the final race worse
in the [main-color trial](RAYMAN_MAIN_COLOR_SOURCE_COMPARE_2026-09-26.md).

After two inconclusive trials on one gap, record the result and re-rank the
gap or change the capture/replay seam. Keep known `DEVICE_HUNG` indexed
families out of live admission until a single draw's GPU failure is explained
and a longer route survives. Defer optional postprocessing and exact texture
parity when neither improves the playable final frame. Keep the complete
ordered stream and original shaders as the long-term ownership path; do not
grow a second set of title-specific material guesses.

## Work already available

- `graphics_hooks.cpp` observes prepared/final GPU draws, has exact current-
  frame scene snapshots and resource generations, and already exposes an
  opt-in race admission gate. The six-family capture is a source of known-good
  draw/resource examples, **not** the required structure for every future draw.
- `native_output_track.cpp` draws owned geometry into the existing D3D12
  output. `guest_output_renderer.cpp` owns the pre-UI and final-output hooks,
  native toggle, and whole-frame compatibility fallback. Extend these paths.
- The installed native shader pack contains vertex and pixel DXIL plus
  binding metadata. Current native track output uses original vertex shaders
  but mostly handwritten pixel materials. Use the existing pack/loader to
  bring original pixel programs into native replay; do not rebuild the corpus.
- Existing race, UI stress, exact-scene, mode-boundary and hot-toggle scripts
  provide the checks below. Existing CPU/render captures are baselines, not
  blockers to the first visible frame.

## Execution backlog

### RAY-00 — choose the complete-frame capture seam

The first bounded pair selected the consumed GPU command seam and reproduced
the missing guest UI pass; see the [evidence](RAYMAN_FRAME_STREAM_SEAM_2026-09-25.md).
The [ordered event capture](RAYMAN_ORDERED_FRAME_EVENTS_2026-09-25.md) now
records regular draws, optimized clears and copies/resolves at that seam.
The 48 internal ordinal gaps in its sampled frames are confirmed mip skips;
full immutable resource inputs and offline replay remain open.
An opt-in [owned UI draw capture](RAYMAN_UI_FRAME_CAPTURE_2026-09-25.md)
now preserves one HUD-visible frame's ordered UI draws and CPU inputs. It
does not yet capture texture pixels, clears, resolves, or the full race frame.
The [UI replay pilot](RAYMAN_UI_REPLAY_PILOT_2026-09-25.md) presents the owned
scene plus 116 original untextured HUD draws from source frame 6803. A separate
run at source frame 6801 again had no UI producer, so the scheduling gap is
confirmed intermittent; full-frame capture and replay are still open.
The [textured UI pilot](RAYMAN_TEXTURED_UI_REPLAY_2026-09-25.md) now replays
all 166 HUD draws with 16 pinned texture versions in one successful frame.
Two adjacent repeat runs had no UI producer at the selected frame, making
the scheduling/reuse gap the immediate obstacle to live admission.
The [adjacent-frame census](RAYMAN_UI_PRODUCER_CADENCE_2026-09-25.md)
confirmed three no-producer frames among ten while the compatibility HUD
remained visible. Indexed-vertex trimming reduced a complete UI capture
from about 67 MB to 2 MB without changing the rendered HUD.
The [ordered draw-state capture](RAYMAN_ORDERED_DRAW_STATE_2026-09-26.md)
now preserves replay-critical draw state and exact texture-version keys in
the selected command stream. A source-5000 HUD-gap frame explicitly names
retained UI source 4999 and exports its complete 166-draw fixture. World
geometry bytes are now owned in the selected-frame
[geometry artifact](RAYMAN_ORDERED_GEOMETRY_2026-09-26.md): all 3,100 draws
in that source-5000 run have verified index and vertex payloads. The
[texture-pin capture](RAYMAN_ORDERED_TEXTURE_PINS_2026-09-26.md) now retains
all 415 exact texture versions in a separate selected-frame run, including
the previously unsupported 3D texture. Intermediate target versions and
ordered execution still need work before a full-frame image is replayed.
The [final-bound input artifact](RAYMAN_ORDERED_DRAW_INPUTS_2026-09-26.md)
now owns the shader constant and fetch state for every draw in another
selected-frame run. Its 2,965 draws also have complete geometry and exact
texture pins; all required shader specializations exist in the installed
pack. The remaining RAY-00/01 gap is native execution of the ordered target
and copy sequence, not input identification for that sampled frame.

- [ ] From one moving-race frame and one known HUD-gap frame, record one
  ordered stream of draw, clear, resolve and output-target events. Each event
  identifies its source/output frame, target, shaders, state and immutable
  inputs or an exact existing resource version. Account for indirect and
  deferred draws as well as UI/font draws. Represent a no-producer frame's
  retained UI target/version explicitly.
- [x] Compare the candidate title-level D3D hooks with the actual consumed
  GPU work. If they do not cover the frame, use the existing prepared/final
  draw observation boundary and add only the missing ordered target/clear/
  resolve events there. The earlier wrapper census found 132,568 candidate
  matches were EDRAM copies, so do not repeat a broad wrapper hunt.
- [x] Save a compact event-count/target-order report for those two frames,
  including UI present/absent. Decide the seam from this bounded check and
  move to replay. Do not make complete game-wide coverage a prerequisite.

**Done when:** one selected stream explains the race image's ordered work and
its missing events are explicit. If the HUD is absent at the producer in the
gap frame, record that fact and address its scheduling cause before takeover.

### RAY-01 — replay one captured frame offline

The textured pilot has a recognizable native scene and complete captured
HUD, but does not consume the full ordered world event stream. Its image and
constraints are recorded in the
[textured replay evidence](RAYMAN_TEXTURED_UI_REPLAY_2026-09-25.md).
The [six-family world replay](RAYMAN_ORDERED_WORLD_REPLAY_2026-09-26.md) now
issues supported world draws by backend sequence rather than by family. This
improves their draw ordering, but clears/resolves, unsupported draws and the
separately captured HUD are not yet one authoritative event stream.
The selected ordered stream now includes the state and retained-UI source
needed to drive that replay. Its geometry inputs are complete for the
selected source-5000 run, and exact texture versions are pinned as immutable
GPU copies for same-process replay. Intermediate target contents and ordered
execution remain incomplete. The final-bound shader state is also exported
per draw, and the installed shader pack covers every sampled variant.
The [ordered-dispatch pilot](RAYMAN_ORDERED_DISPATCH_PILOT_2026-09-26.md)
now feeds the selected event order to all 2,477 existing six-family draws in
source frame 5001 and saves a recognizable shadow image with the HUD from that
same frame. The other 1,796 draws and 94 copies remain counted but unexecuted.
The selected-frame replay applies one of 20 captured clears: the main scene
color/depth clear at its stream ordinal when its target and rectangle match
the native surface. Selected-frame takeover also requires the UI draws to be
the stream's final draw suffix and the last event to be a successful full-size
copy from that UI surface. Intermediate target operations and original pixel
programs remain next.
The [ordered color-tile pilot](RAYMAN_ORDERED_COLOR_TILES_2026-09-26.md)
now copies the three completed main-scene regions into an owned 1280×720
native target at their captured ordinals and presents that assembled image.
Frame 5001 promoted with readable HUD and a normal continuous-race exit.
The copy remains approximate: native source is single-sample UNORM, while
the guest source is 4× MSAA float; initial color, depth and later texture
versions still depend on compatibility work.
The [ordered depth-version pilot](RAYMAN_ORDERED_DEPTH_VERSIONS_2026-09-26.md)
now snapshots the full native depth surface at each paired depth-copy
ordinal, preserving three owned versions because D3D12 cannot partially
copy a depth/stencil subresource. The selected frame again promoted with
readable HUD and a normal race exit. The initial depth texture and later
shader reads remain compatibility-backed.

The [initial color-version pilot](RAYMAN_INITIAL_COLOR_VERSIONS_2026-09-26.md)
now snapshots two full-size native color versions at their captured copy
ordinals, around the feedback draw. They are not bound to consumers yet:
the offscreen producer and feedback draw still need native replay.
The [producer-input pilot](RAYMAN_INITIAL_COLOR_PRODUCER_INPUTS_2026-09-26.md)
now exposes the bounded post-clear draw payload to the live replay. All 13
draws have owned geometry/final state, and the native backend retrieves their
original shader bytecode and immutable textures. The offscreen pilot now
uses those payloads.
The [offscreen producer pilot](RAYMAN_INITIAL_COLOR_OFFSCREEN_2026-09-26.md)
now issues eleven original-shader color draws and the feedback draw into an
owned RGBA target and makes both ordered copies from it. The guest first
version is a full scene image; a diagnostic view of the native version shows
large flat regions instead. It remains detached from consumers while target
history, depth and binding differences are resolved.
The [depth-prepass census](RAYMAN_OFFSCREEN_DEPTH_PREPASS_2026-09-26.md)
identifies the missing offscreen input: 498 depth-writing draws precede its
eleven depth-tested color draws. The selected-frame pilot now snapshots the
1× source at the depth resolve and binds a copied DSV. Its diagnostic preview
shows some new structure but still lacks most lower-scene detail, pointing
to missing starting color history.
The [feedback seed pilot](RAYMAN_INITIAL_COLOR_OFFSCREEN_2026-09-26.md)
found that a pre-draw full-resource cache snapshot is nearly white despite
the guest's complete first resolved version. It now seeds the second native
version from that pinned first version before the original feedback draw.
The diagnostic second version has recognizable crowd, scenery and car;
the [paired comparison and guarded consumer pilot](RAYMAN_SECOND_COLOR_CONSUMER_2026-09-26.md)
then found a 97.342% exact match in the sampled central region and routed
one car-material read to it. The complete frame still has major background
artifacts; other consumers remain compatibility-backed.
The [main-scene original draw pilot](RAYMAN_MAIN_SCENE_ORIGINAL_DRAW_2026-09-26.md)
also issues one captured textured strip in each of the three color tiles at
its stream ordinal. A short native race passed with readable HUD, but the
saved image shows no clear visual gain; 36 draws from that unsupported
main-color census remain. Prioritize a visibly missing layer next.
The [index-input and large-scene trial](RAYMAN_INDEX_INPUT_AND_LARGE_SCENE_TRIAL_2026-09-26.md)
corrected guest-byte-order 16-bit indices in the offscreen pilot. Its native
texture still lacks the earlier scene history. A combined trial of two large
main-color families removed the D3D12 device and was withdrawn. Isolated
follow-up runs exposed the recurring no-producer UI frame: fixed-frame
ordered replay cannot test a new family when the selected frame retains an
earlier HUD target. Address this cadence before another large-draw trial.
The [ordered retained-HUD pilot](RAYMAN_ORDERED_RETAINED_UI_2026-09-26.md)
now admits a selected no-producer frame when its prior complete HUD draw set
and both terminal copies identify the same EDRAM source, despite alternating
guest output addresses. Frame 6782 promoted with UI source 6781 and normal
route exit. The saved selected-frame image from a separate same-frame-HUD run
is recognizable but still shows major yellow background artifacts.
The [first owned post-scene downsample](RAYMAN_NATIVE_DOWNSAMPLE_2026-09-26.md)
and [small reductions](RAYMAN_NATIVE_SMALL_REDUCTIONS_2026-09-26.md) now
execute original 320×192, 64×32 and 32×32 draws from native targets at their
copy ordinals. The 64×32 probe shows a bright but recognizable race image;
the 32×32 output is solid white. Matched guest intermediate probes show dark,
coherent 320×192 and 64×32 images and a nearly black 32×32 image. The
brightness mismatch is visible at the first 320×192 stage, so defer further
small-target replay until the visible world source is improved. The next
320×192 feedback draw reads prior-frame history. Neither that feedback
version nor the final composite is owned.
The [main-color source comparison](RAYMAN_MAIN_COLOR_SOURCE_COMPARE_2026-09-26.md)
shows the native scene is already much brighter than the pinned guest input
to that first downsample. A source-fitted color scale improved intermediate
MAE but made the final race too dark and was removed. Judge visible changes
against the final guest frame, not an intermediate alone.

- [ ] Diagnose the shared `DEVICE_HUNG` in large indexed scene draws: isolate
  one draw with the D3D12 debug layer and DRED, verify index/vertex bounds,
  vertex transform/raster coverage, depth state and draw cost. A flat pixel
  program still hung with 1,024 indices per draw; test a single triangle
  only on a frame whose race HUD and native shadow gate are verified. Keep
  failing families out of live admission.
- [ ] Feed the selected event stream into the existing D3D12 native output,
  retaining draw order, targets, clears, depth, resolves, viewport, scissor,
  blending and texture versions needed by this race frame. Start with the
  existing native geometry path and one useful original pixel-shader pair;
  expand only when a visible region requires it. A selected-frame replay is
  a fixture; move the useful slice to the rolling live path before claiming
  a continuous renderer improvement.
- [ ] Render UI/fonts from the **same** stream and frame as the world. Do not
  recover them with a fixed screen rectangle or a previous-frame guest copy.
  Record unsupported events by count and visible effect, and keep the guest
  output as the comparison reference.

**Done when:** a saved offline image has recognizable road, car and race HUD
from one ordered frame. Missing optional effects can remain listed; no exact
byte match or 90% score is required.

### RAY-02 — replay live in shadow

The [first live shadow segment](RAYMAN_LIVE_SHADOW_2026-09-25.md) has six
consecutive saved native images with updating world and readable HUD while
compatibility remains displayed. One no-producer UI frame correctly stays
on compatibility. The official moving and UI stress routes also exited
normally with distinct shadow images. Retained UI state across producer gaps
remains RAY-03 work.

- [x] Use the same replay path on continuously captured race frames while the
  compatibility image remains the displayed output. Reuse current-frame
  resource pinning, admission and route automation. Avoid the old full-fixture
  serialization, stage waits and readback in the live feed.
- [x] Run the moving-race and UI-admission stress routes. Inspect adjacent
  frames for world/HUD alignment, missing UI producers, changing resources and
  gross capture overhead. Fix only failures that prevent coherent continuous
  replay; keep a short list of visible gaps for RAY-04.

**Done when:** a short moving segment produces consecutive native shadow
frames with readable, updating world and HUD, and no unbounded capture stall.

### RAY-03 — own the visible race output

The [bounded takeover pilot](RAYMAN_NATIVE_TAKEOVER_PILOT_2026-09-25.md)
first copied a complete scene-plus-HUD scratch frame into guest output only
when both passes succeeded. Its hot-toggle route showed native presentation,
full compatibility fallback on a no-producer HUD frame, and recovery after
turning native output off and on.
The [retained HUD pilot](RAYMAN_RETAINED_UI_2026-09-25.md) now replays the
immediately previous complete HUD on a no-draw frame when no target-write
event intervened. One observed gap stayed native, but the exact guest target
version and longer-run cadence still need validation.
The [continuous race run](RAYMAN_CONTINUOUS_NATIVE_RACE_2026-09-26.md) extends
opt-in promotion beyond the 24-frame pilot. Scripted runs covered more than
300 moving frames, hot toggle, pause/resume and return to compatibility.
The native road and car remain visibly rough; the supported world draws now
follow backend order, but the complete frame does not yet consume one
authoritative ordered event stream. The scripted RAY-03 presentation and
fallback milestone is met; unscripted playability and visible gaps remain
RAY-04 work.

- [x] Route the complete native frame through the existing guest-output hook
  and hot toggle. Select one output target from the captured target/resolve
  chain. Admit only a current, complete frame; otherwise show the **entire**
  compatibility frame. Keep non-race modes on compatibility output.
- [x] Run a scripted moving race, toggle both ways, pause/resume, and leave the
  race. Check for blank/stale frames and world/HUD alternation. Validate the
  intended resolution first; reject unsupported output modes cleanly.

**Done when:** a player can drive that scripted race segment with native
world and readable HUD for successive frames, then return safely to normal
output. This is the first usable renderer milestone.

### RAY-04 — close only visible race gaps

The [first original car pixel program](RAYMAN_ORIGINAL_CAR_PIXEL_2026-09-26.md)
replaces one bounded no-texture approximation. The main eight-texture body
remains placeholder shaded and is the next car-material target.
The [body texture snapshot run](RAYMAN_BODY_TEXTURE_SNAPSHOTS_2026-09-26.md)
preserves all eight source-frame texture versions, including two cube maps.
The [original body program](RAYMAN_ORIGINAL_BODY_PIXEL_2026-09-26.md) now
binds those resources and its constants in live presentation. It adds
specular shading, but the rear paint is darker than the placeholder.
The [original glass program](RAYMAN_ORIGINAL_GLASS_PIXEL_2026-09-26.md)
replaces the flat red rear window with dark glass across a full native race
route. Compare a matched Xenos frame
and inspect sampled values before calling this car-quality gap closed. The
scripted native and Xenos runs diverged in game time and car position at the
same output frame, so an unscripted drive remains essential for playability.
The [dynamic car blend check](RAYMAN_DYNAMIC_CAR_BLEND_2026-09-26.md) removed
a zero-only constant restriction from the original no-texture car shader.
This closed a repeated `prepare_remainder` fallback interval in a short race
route; one later `track_structure_texture` rejection remains to diagnose. The
mode-boundary route passed after this change: race output was native and pause,
free roam, transition and title stayed on compatibility. Its exact-sky color
threshold was corrected for a race view mostly covered by a roadside sign.
The first original track structure program now uses its three pinned
source-frame textures, pixel constants and bool word. In the corrected
title-to-race route, the former flat-purple structure gains truck and support
detail in the native frame at output 5020. A large gray panel and nearby
purple terrain remained. The seven-draw
`5DB1ECF39EA11DB0`/`6508BAC22C4E1720` pair now uses the same original
pixel path with its own fetch order and 48 pixel words; a subsequent full
route shows the large roadside arrow sign textured yellow and black.
Nearby terrain and structures remain visibly rough. Evidence is in
`.local/ray-ui-native-promotion-20260925/structure-title-entry-long-output`
and `panel-original-long-output`. These visual gains do not establish race
quality or performance completion.
The 30-draw `6934E161812AB10B`/`B98566FB7CE14699` family also now uses its
original six-texture pixel program. The native race route replaces a flat
purple trackside area with textured vegetation and earth colors, but the
underlying area is still far too dark. Compare it with a matched compatibility
view and inspect alpha/depth before marking terrain playable. Evidence:
`.local/ray-ui-native-promotion-20260925/terrain-original-long-output`.
A same-run native/compatibility/native toggle at frames 5038, 5044 and 5055
kept the HUD and full-frame fallback intact. Compatibility confirms dark soil
is expected, but reveals missing building, crowd and lighting layers in native
output. The 39-draw `0CBC533419F61E0D`/`56D45C45966FD938` building
family now uses its original five-texture pixel program. Its first trial
showed neon-yellow surfaces; a corrected sampler table and larger bounded
material snapshot pool now admit the facade in a full race route. The route
promoted 836 frames and recovered from 67 whole-frame compatibility fallbacks
at `prepare_remainder`. See the
[building material evidence](RAYMAN_BUILDING_MATERIAL_2026-09-26.md).
The live draw consumes the procedural-character fixture and its recorded
tiled viewport/scissor. The separate character-manager family now joins
backend draw packets and final state without title records and replays in
native race output. At source frame 5000, 30 packets/90 manager draws were
admitted and spectator silhouettes are visible. This closes the missing
crowd *geometry* gap on the scripted route; material color and whole-frame
ordering still need a matched comparison. See
[manager live evidence](RAYMAN_MANAGER_LIVE_2026-09-26.md).
The [manager material capture](RAYMAN_MANAGER_MATERIAL_CAPTURE_2026-09-26.md)
now owns per-draw pixel constants, bool/loop words and two final texture
versions. The original pixel shader now gives visible spectators textured
clothing and skin when both source textures are pinned 2D views. Other draws
retain the flat material when the texture version cannot be pinned. Adding
manager fetches 0 and 13 to the SDK's normal snapshot filter keeps the crowd
textured through a 309-frame scripted race with no native rejection or
snapshot-limit warning. A same-session native/compatibility/native toggle
shows comparable spectator textures and safe recovery. Background structure,
lighting, ground and car materials still differ visibly; an unscripted drive
remains open.
The [layered material trial](RAYMAN_LAYERED_MATERIAL_TRIAL_2026-09-26.md)
ran the original one-texture pixel program on 269 procedural draws without
closing the flat-ground or missing-background gap. The trial was removed.
The [shadow-only frame comparison](RAYMAN_SHADOW_ONLY_FRAME_COMPARISON_2026-09-26.md)
exposed a misleading live comparison: a promoted native image reappeared in
a reused guest-output buffer three output frames later. With promotion off,
native shadow 5001 and near-aligned guest output 5002 differ strongly: flat
sky, unlit buildings and asphalt, missing car detail/shadow and a degraded
navigation graphic. Use shadow-only guest references for visual triage.
The [ordered support census](RAYMAN_ORDERED_SUPPORT_CENSUS_2026-09-26.md)
joins every selected-frame draw to its native family. Of 1,377 unsupported
draws, 1,049 precede the main scene, 63 target the main scene, and 265 follow
it (including 166 HUD draws currently replayed separately). The first
1280×720 copy writes guest base `497831936`, which 11 offscreen draws and
six tiled main-scene draws sample across three pinned texture generations.
Prioritize that intermediate target/version chain before expanding materials
for already visible geometry.
The [color alias probe](RAYMAN_COLOR_TARGET_ALIAS_PROBE_2026-09-26.md)
confirmed the two main `RB_COLOR_INFO[0]` values select float-format aliases
at the **same** EDRAM base. Isolating their draws removed complementary
parts of the car/crowd and structures/ground. A float-scratch-only trial did
not improve the saved image and was removed. Preserve that alias and include
the resolve/output conversion in any later format change.
The [ordered copy-input capture](RAYMAN_ORDERED_COPY_INPUTS_2026-09-26.md)
now records source selection, EDRAM address/format, source and destination
rectangles, and validity for every copy. It proves the 1280×720 copy to
`497831936` and the three main tile updates are **depth** resolves; the
paired main tile **color** resolves write another guest texture, while the
terminal full-frame copy selects color. The initial full-size color resolve
to `484626432` has 1,274 later texture bindings in this sampled frame,
versus 15 bindings to the depth destination. All 91 nonzero-size copies have
complete in-bounds inputs. Native execution of these resolves is still open.
The [paired live baseline and flat-color lookup](RAYMAN_LIVE_FINAL_BASELINE_2026-09-26.md)
identified the large green/gray ground polygons as two supported track
families using flat fallback colors. Fetch-0 sampling for those 30 draws
removes both exact flat colors across five consecutive moving native shadows,
with same-run guest references and a normal route exit. This is a visible
RAY-04 gain, while earlier color resolves remain an RAY-00/01 ownership gap.
The [late-race facade check](RAYMAN_FACADE_NEON_2026-09-27.md) caught a
persistent pure-yellow artifact missed by the short route. Isolating the
five-texture original program removed it; the retained captured fetch-5
approximation also removes it across eleven moving stress frames while
keeping visible structure texture. A paired later shadow run and hot-toggle
pass. This is a playability correction, not full material parity.
The [car stencil comparison](RAYMAN_CAR_STENCIL_2026-09-27.md) found that
guest stencil rejects a black rear overlay that native applied without a
stencil target. Omitting that overlay improves the rear-car region in four
paired moving frames. Exact stencil ownership remains a later ordered-replay
task if the missing detail proves visible during driving.

- [ ] Fix the largest problems seen in RAY-03 in this order: missing UI
  producer or ordering; opaque/missing car; unreadable road/terrain; major
  alpha/depth errors; only then secondary scenery/effects. Reuse already
  captured shader identities, textures and constants. Prefer original packed
  pixel programs over a growing set of handwritten material guesses.
- [ ] Perform one longer unscripted drive and the existing mode-boundary
  route. Keep an explicit list of unsupported effects and modes; approximate
  those that do not affect playability.

**Done when:** a sustained race is visually coherent and responsive enough to
play, with stable HUD and whole-frame fallback. No all-mode, exact-match or
performance percentage gate.

### RAY-05 — remove proven duplicate rendering cost

The first [live performance comparison](RAYMAN_LIVE_PERFORMANCE_2026-09-27.md)
found 187.81 ms median native frames against 23.54 ms compatibility frames
on the same scripted race window. Exact-version texture snapshot reuse
reduced native time to 118.34 ms while preserving native promotion and
fallback. This is still too slow for the intended drive. Treat the next
measured duplicate-work slice as part of the RAY-04 playability gate; defer
secondary sky/material polish until responsiveness is credible.

- [ ] Profile the live native route against compatibility with existing CPU
  and GPU tools. Remove the largest measured cost first, especially duplicate
  capture/replay work or unnecessary readback; retain changes only after a
  matched whole-frame A/B and a short driving check.
- [ ] Suppress replaced Xenos rendering **only** after proving its target is
  not read by guest queries, resolves, memexport, CPU readback or later draws.
  Keep guest-visible producer work or supply an equivalent result. Expand the
  supported race envelope when measurements justify it.

**Done when:** the replacement avoids proven duplicate visual work and has
measured frame-time improvement without breaking the race or compatibility
fallback. A Rayman-style null backend is an optional later implementation,
not a prerequisite for the first usable renderer.

## Next implementation slice

The 300-frame scripted route and hot toggle establish continuous presentation,
but not a comfortable drive. The first matched scripted window measured
94.88 ms median native versus 23.54 ms compatibility. Immutable manager
snapshot reuse reduced prepared-observer CPU from 14.50 to about 8.6 ms;
two follow-up frame medians were 90.97 and 94.62 ms. Prepared snapshots still
take about 11 ms and native output about 30–35 ms. A bounded split assigned
about 5.2 ms of snapshots to track draws and 3.8 ms to main-scene probes;
even eliminating all snapshots would leave an approximately 80 ms frame.
Stop treating snapshot micro-optimization as the route to a usable cadence.
The full ordered replay is still a selected-frame fixture.
Use the following gates to avoid another sequence of isolated pilots:

1. [x] **Bound the capture-only optimization path.** The
   [performance ledger](RAYMAN_LIVE_PERFORMANCE_2026-09-27.md) records the
   repeatable manager-observer reduction, an inconclusive upload-buffer pool
   that was removed, and the snapshot cost split. Reopen one of these paths
   only if a new trace shows it blocks live ordered ownership.
2. [x] **Trace the chosen live gap to its source input.** The
   [late-race paired capture](RAYMAN_LATE_RACE_UI_LINEAGE_2026-09-27.md)
   joins a green guest and native-bound route texture to a dark native first
   UI draw. Pixel history found an extra back-facing native triangle covering
   the route. Matching guest raster state restored the green navigation in
   three same-run moving frames while preserving the readable HUD. Keep known
   `DEVICE_HUNG` families out of admission until one isolated draw survives.
3. [x] **Remove the confirmed rear-car stencil mismatch.** The
   [car stencil comparison](RAYMAN_CAR_STENCIL_2026-09-27.md) identifies the
   rejected guest overlay and verifies a bounded native omission across four
   paired moving frames. Keep it only while the car remains readable in a
   longer drive; implement guest stencil ownership if the omitted detail
   becomes a visible blocker.
4. [ ] **Check actual driving and boundaries next.** Run a longer unscripted
   race with steering and a mode boundary. Record control response, HUD
   stability, fallback frequency, blank/stale frames and the first visible
   defect that interferes with driving. Repeat the scripted sustained,
   hot-toggle and mode-boundary checks after ownership or suppression
   changes. The [paired mode-boundary control](RAYMAN_MODE_BOUNDARY_CONTROL_2026-09-27.md)
   confirms that `title-settled` background noise also occurs with native
   disabled; the current native route passed its boundary verifier. The
   underlying title artifact remains a compatibility-renderer issue. The
   drive is the RAY-04 gate.
5. [ ] **Fix the next observed driving blocker.** Use a same-run final-frame
   pair and its exact source inputs. The
   [same-output color trace](RAYMAN_LIVE_SCENE_COLOR_CHAIN_2026-09-27.md)
   locates the pale ground and missing background upstream of the guest's
   final composite: guest scene draws use a four-sample float target and a
   packed color resolve, while native copies a single-sample UNORM target.
   The captured copy's -2 exponent bias and four-sample mode predict the
   guest packed pixel at a road location; a 5,000-pixel probe supports that
   conversion across the first tile. This is a candidate cause, not a gate
   for all scene work. If it is the drive blocker, reproduce one native tile
   against the pinned guest version, then move the needed original shaders,
   constants, texture versions, target alias, clears and copies into rolling
   frames. Retire the scene-snapshot approximation only after several final
   moving frames improve. The 320×192 and 640×360 composite inputs remain
   separate dependencies.
6. [ ] **Attack the measured frame-time blocker.** The latest repeated native
   medians are about 91–95 ms versus 23.54 ms compatibility. The
   [current trace checkpoint](RAYMAN_POST_STENCIL_PROFILE_2026-09-27.md)
   confirms about 93 ms with large guest draw-issue and native output CPU
   slices. Remove the largest qualified cost with a paired promoted frame
   and short driving check. Do not spend another slice on snapshot
   micro-tuning: its measured ceiling cannot make the renderer responsive.
7. [ ] **Qualify one duplicate guest pass only with dependency proof.** The
   [guest-visible dependency ledger](GUEST_VISIBLE_RENDER_DEPENDENCIES.md)
   does not yet qualify any target for suppression. For one candidate, prove
   its resolves, later fetches, queries, memexport and CPU visibility are
   preserved or replaced before suppressing it; otherwise leave Xenos
   execution intact. Measure promoted-frame latency before and after; a
   faster fallback is not a gain. A null backend is optional, not a shortcut
   around guest-visible side effects.

The selected-frame post-scene reductions, final-composite trials, depth and
color-version experiments, and large-index isolation remain documented above
and in their linked evidence files. Reopen one only when a live final-frame
gap requires it. Keep the 1280×720 color-format aliases `0x00030000` and
`0x000C0000` over their shared EDRAM base when that work resumes.
