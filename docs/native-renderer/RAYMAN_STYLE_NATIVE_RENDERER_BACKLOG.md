# Native renderer: complete-frame backlog

Status: active delivery plan, 2026-09-25. This supersedes the delivery order
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
Payload and 48 unobserved draw ordinals per sampled frame remain open.
An opt-in [owned UI draw capture](RAYMAN_UI_FRAME_CAPTURE_2026-09-25.md)
now preserves one HUD-visible frame's ordered UI draws and CPU inputs. It
does not yet capture texture pixels, clears, resolves, or the full race frame.

- [ ] From one moving-race frame and one known HUD-gap frame, record one
  ordered stream of draw, clear, resolve and output-target events. Each event
  identifies its source/output frame, target, shaders, state and immutable
  inputs or an exact existing resource version. Account for indirect and
  deferred draws as well as UI/font draws.
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

- [ ] Use the same replay path on continuously captured race frames while the
  compatibility image remains the displayed output. Reuse current-frame
  resource pinning, admission and route automation. Avoid the old full-fixture
  serialization, stage waits and readback in the live feed.
- [ ] Run the moving-race and UI-admission stress routes. Inspect adjacent
  frames for world/HUD alignment, missing UI producers, changing resources and
  gross capture overhead. Fix only failures that prevent coherent continuous
  replay; keep a short list of visible gaps for RAY-04.

**Done when:** a short moving segment produces consecutive native shadow
frames with readable, updating world and HUD, and no unbounded capture stall.

### RAY-03 — own the visible race output

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

## First implementation slice

Start with RAY-00, then immediately build RAY-01 from the chosen stream. The
first code change should add the smallest ordered event feed at the selected
existing observation seam and a single-frame replay consumer. It should
produce an inspectable image, not another family-specific evidence ledger.
After that image exists, move straight to RAY-02 and RAY-03. Reassess this
backlog only when a concrete missing producer or guest-visible dependency
forces a different seam.
