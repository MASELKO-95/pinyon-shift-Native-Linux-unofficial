# Native renderer: complete-frame backlog

Status: active delivery plan, updated 2026-09-26. This supersedes the delivery
order
in the [scene-native backlog](SCENE_NATIVE_RENDERER_BACKLOG.md); that document
remains the implementation and evidence ledger. The
[Rayman study](RAYMAN_NATIVE_RENDERER_RESEARCH_2026-09-25.md) explains the
reference architecture and its limits for FH1.

## Target and rules

Ship a **usable, opt-in native race renderer quickly**: one continuous moving
race with coherent world, cars and readable HUD. Approximate nonessential
effects are acceptable. Measure and optimize frame time after this works.
Menus, free roam and unsupported race states can use the existing compatibility
renderer. A rough or incomplete frame is acceptable in offline/shadow bring-up,
but never replace a visible compatibility frame with stale, blank or
unreadable output. Keep the whole-frame fallback and hot toggle.

Follow Rayman's delivery sequence: **ordered capture → offline replay → live
shadow → native presentation → fill visible gaps → remove duplicate work**.
Reuse FH1's D3D12 backend and current shader/resource infrastructure; this is
not a Vulkan port or a new renderer abstraction. Rayman's GPL source is an
architectural reference, not code to copy into this BSD repository.

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
now executes the original eight-rectangle 320×192 draw from native tiled
color and preserves its target at the captured resolve ordinal. An opt-in
shadow probe shows a coherent small race image; the ordinary full-scene
shadow remains intact. The later 320×192 reduction/feedback version is not
yet owned or bound to the final composite.

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
  expand only when a visible region requires it.
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
authoritative ordered event stream.

- [ ] Route the complete native frame through the existing guest-output hook
  and hot toggle. Select one output target from the captured target/resolve
  chain. Admit only a current, complete frame; otherwise show the **entire**
  compatibility frame. Keep non-race modes on compatibility output.
- [ ] Run a scripted moving race, toggle both ways, pause/resume, and leave the
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

### RAY-05 — optimize after the renderer is usable

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

Use the continuous opt-in run as the RAY-03 test bed. The isolated
track-texture resolution miss did not recur on the next route and is covered
by whole-frame fallback. Use the new ordered color-tile replay as the
target-copy pilot. Compare the initial offscreen RGBA target against the
pinned guest version at the same ordinal; restore the target and depth
history needed to make its scene image coherent before binding it to
consumers. This mismatch is not a blocker to a usable first renderer: keep
the pinned guest version while issuing remaining visible world draws.
Preserve the tiled color texture's retained starting version, replace the
initial guest depth texture when its producer is ready, and route owned
color/depth versions to later consumers. Use the stream's owned geometry,
pinned textures and final state to issue remaining visible main-scene draws.
Choose the next draw family by visible missing coverage, not merely by a
small input shape: the first three-draw textured strip ran successfully but
did not visibly change the saved race image.
The [main-scene family isolation](RAYMAN_MAIN_SCENE_FAMILY_TRIALS_2026-09-26.md)
used the retained-HUD frame to test the 8,700-index atlas and a distinct
one-texture indexed strip separately. Both caused D3D12 `DEVICE_HUNG` after
submission and were removed. Stop admitting large indexed families by input
shape alone. Diagnose one draw's GPU bindings and translated shader under
the D3D12 debug layer and DRED, then retry only when its cause is addressed.
The [tile isolation](RAYMAN_LARGE_SCENE_TILE_ISOLATION_2026-09-26.md)
now shows the 8,700-index family is safe for a full first tile and the first
two tiles together, but its third tile alone removes the device. A first-tile
NDC/viewport mapping lets the third tile finish alone; all three still hang
together. Measure the translated vertex path and per-tile GPU duration before
admitting this family. Its safe trial images did not visibly close the
background gap.
Until that path is stable, prioritize the already captured offscreen
color/depth history and its pinned guest consumer comparison rather than
admitting another large scene family.
The uncontaminated shadow-only comparison makes the first full-scene color
history and its lighting/road consumers the immediate visual priority.
Recheck native output against a near-aligned guest reference with promotion
off before claiming a visible gap closed; a later guest buffer may contain
an earlier promoted native frame.
Directly seeding the main scene from the pinned first color resolve produced
a white sky and washed-out HUD, so trace the later tiled color and lighting
chain instead of using that early resolve as a backdrop.
The selected shadow-only stream ends its main color tiles at ordinal
`11822064`, then reaches two 1280×360 five-texture composite draws at
`11822215`/`11822216` just before the HUD suffix. A bounded replay trial
confirmed rectangle expansion and stage-specific descriptor mappings, but
its detailed output sampled a pinned guest 1280×720 color texture and was
rotated 180 degrees. The trial was removed; see the
[shadow comparison](RAYMAN_SHADOW_ONLY_FRAME_COMPARISON_2026-09-26.md).
The [owned-input trial](RAYMAN_COMPOSITE_INPUT_CHAIN_2026-09-26.md) completed
that trace: fetch 0 is the same three-tile color resolve already assembled in
native `color_tiles`. Replacing it and using an unrotated blit gave an upright,
recognizable shadow, but harsh color artifacts made it worse than the current
native output. Fetches 2 and 5 still came from guest-produced 320×192 and
640×360 target versions. The trial was removed. The first 320×192 draw from
owned color now runs in the selected ordered frame. Next, replay its smaller
reduction and 320×192 feedback passes, preserving exact target versions;
then replay the depth/color-fed 640×360 pair. Compare each intermediate to
its guest version before retrying the final composite with only owned
scene-dependent inputs. A composite of the guest scene does not count as
native coverage.
The seeded second native color version now matches the pinned guest second
version closely in one sampled region, and one car draw consumes it under
the existing whole-frame fallback. Expand replacement only to a consumer
whose pinned version and sampled region are verified. Do not use the
pre-draw full-resource cache snapshot as the starting image: it is nearly
white because the EDRAM history has not been materialized there. The first
native version still lacks that history; keep the guest first version for
feedback until its native source can be made complete.
Save the resulting full-frame image and integrate the same-frame HUD at its ordered
suffix.
Treat `0x00030000` and `0x000C0000` as format aliases over one EDRAM base,
not separate color targets.
Perform the longer unscripted drive required by
RAY-04 before profiling duplicate work.
