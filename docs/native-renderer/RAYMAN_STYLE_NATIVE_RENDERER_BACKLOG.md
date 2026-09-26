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

Use the continuous opt-in run as the RAY-03 test bed. Explain the
`prepare_remainder` fallback in the scripted race, then perform the longer
unscripted drive required by RAY-04. Continue RAY-00/01 toward one
authoritative ordered frame stream. Profile only after that drive is playable.
