# Development findings and priorities

Consolidated from the development records at `53f9bf9` (2026-09-10).
This is the current starting point for development, not a release announcement.
At main checkpoint `2fa804c`, the source pins ShiftGlue
`a6905c39aed6353916903ada3e0c840bbc39caa2`;
older binary hashes in individual experiment reports describe those experiments.

## Documentation map

| Need | Read |
| --- | --- |
| Build or recover an installation | [Building](BUILDING.md), [troubleshooting](TROUBLESHOOTING.md) |
| Configure experimental graphics | [Graphics recovery/settings](TROUBLESHOOTING.md) |
| Primary renderer roadmap | [Xenos retirement backlog](native-renderer/XENOS_RETIREMENT_BACKLOG.md): a content-generic FH1 native renderer that replaces the Xenos backend, following the [Rayman reference](native-renderer/RAYMAN_NATIVE_RENDERER_RESEARCH_2026-09-25.md) |
| Current findings and retained changes | This document |
| Previous renderer and performance work | [Research reference](native-renderer/RESEARCH.md): retired plans, journals, failed trials and their Git checkpoints; [CPU profiling procedure](native-renderer/CPU_HOTSPOT_PROFILING.md) |
| Extend the original game UI | [UI API research and implementation tasks](UI_API_PLAN.md) |
| Reproduce retained renderer changes | [Owned depth](native-renderer/A6_OWNED_DEPTH_RETENTION.md), [reflection mips](native-renderer/REFLECTION_MIPMAP_REPLACEMENT.md), [Carson cache fix](native-renderer/CARSON_GEOMETRY_CACHE_FIX.md) |
| Produce and validate artifacts | [Artifact production](native-renderer/P1_ARTIFACT_PRODUCTION.md), [shader pack contract](native-renderer/SHADER_PACK_FORMAT.md), [shader capture](native-renderer/CANDIDATE_SHADER_CAPTURE.md), [render tests and native controls](native-renderer/FH1_RENDER_TEST_AUTOMATION.md) |
| Investigate user reports | [September 10 issue review (historical)](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/GITHUB_ISSUE_TRIAGE_2026-09-10.md) |
| Release behavior and distribution | [Changelog](../CHANGELOG.md), [preview notes](releases/0.1.2-preview.3.md), [legal](LEGAL.md) |

The [renderer research reference](native-renderer/RESEARCH.md) consolidates the
retired replay, provenance, world, vehicle and batching investigations. Exact
historical documents remain accessible through its Git checkpoint. The remaining
renderer files describe current formats/procedures, retained changes or explicitly
bounded historical qualification contracts; they are not competing roadmaps.

## What the renderer actually does

FH1 CPU code is recompiled, and `rexgpu-fh1.dll` uses locally generated offline
DXIL packs and pipeline preparation. Most scene rendering still uses the
Xenos-compatible D3D12 command, resource and render-target machinery. Native
shader execution alone does not retire that machinery.

The useful architectural unit is a complete resource lifetime: producer,
contents/history, consumers, conflicting writes, reuse and destruction. Reuse
translated shaders where suitable; measure both removed work and whole-frame
cost. Independent native draws or a correct screenshot do not prove a complete
native scene. Full Xenos retirement and lower hardware requirements remain open.

An opt-in native race pilot exists: six captured scene families replayed at
swap time on the Recaro Rush route, with HUD replay, hot toggle and whole-frame
fallback. It runs after Xenos rather than instead of it (about 93 ms against
23.5 ms per race frame) and falls back on events it was not built for, so it is
frozen. The direction is now a content-generic native renderer that executes
every consumed draw, clear and resolve in guest order, following the Rayman
approach; see the [Xenos retirement backlog](native-renderer/XENOS_RETIREMENT_BACKLOG.md).
Xenos remains the reference and default until that renderer passes the route
matrix; the plan does not revive rejected experiments.

### Retained changes

- Bulk constant/register writes, hash-based execution allowlists and avoiding
  unused per-draw census keys reduced command-processing overhead. The September
  4 heavy 2x route reached 57.51 FPS (median of three run medians), with 17.390 ms
  median frame time and 17.241 ms median GPU span. Later heavy 1x runs after
  draw-key removal measured 15.808–16.426 ms. These are different historical
  workloads, not a current FPS guarantee or a matched Xenia comparison.
- **Owned depth clear:** A1–A6 are complete only for the bounded symmetric 1x
  chain. Scaled rendering keeps compatibility clears. The unchanged 2x candidate
  failed North Carson p99 retention (+24.46% initially, +90.09% in the longer
  comparison). [Contract](native-renderer/OWNED_DEPTH_CHAIN_CONTRACT.md) and
  [retention evidence](native-renderer/A6_OWNED_DEPTH_RETENTION.md).
- **Reflection mipmaps:** enabled for validated symmetric 1x/2x/3x inputs
  (3x added 2026-09-12), with fallback and `--fh1_native_reflection_mips=false`
  as the control. Removes 48
  original draws and 48 resolve copies per admitted cube. All six guest lists,
  2,352 packet decodes, state packets, scratch clears and transfers remain.
  Eight clean comparisons show small/mixed whole-frame changes: median +2.15%
  at 1x and -1.35% at 2x. Output, all 54 cube imports, later consumers and captured
  clear history pass their bounded checks. [Evidence and reproduction](native-renderer/REFLECTION_MIPMAP_REPLACEMENT.md).
- **Carson geometry cache:** keep current/previous-frame geometry resident and
  use the existing shared-memory path when the cache cannot admit a new owner.
  The 32 MiB/512-entry budget remains. A short Hot Hatch Hustle 2x comparison
  improves from 100.499 to 33.494 ms median with native mips off and nearly equal
  draw counts. Mips-on 1x/2x smoke also passes. This is not sustained town/race
  acceptance. [Cause, test and limits](native-renderer/CARSON_GEOMETRY_CACHE_FIX.md).
- **Direct reflection-cube import (PERF-05):** changed cubes are written
  directly into the persistent 256×256, six-face, nine-level R10G10B10A2
  texture array with nine compute dispatches instead of a scratch untile and
  54 copies. Median/p95/p99 improved 3.66%/1.42%/6.25%. Default on; rollback
  `--fh1_direct_reflection_cube_import=false`. The owned 1x depth clear
  (PERF-02) stays 1x-only after its 2x retry regressed p95/p99.
- **One submission per frame (PERF-09):** D3D12 keeps each frame in one
  command-list submission instead of submitting at every PM4 primary-buffer
  end: median/p95/p99 −3.4%/−3.7%/−13.6% at 1x and −7.1%/−8.1%/−15.5% at 2x.
  Rollback `--d3d12_submit_on_primary_buffer_end=true`.
- **Deadline-driven guest vblank (PERF-14):** replaces polling; −3.20% median
  and −5.60% p95, 62% fewer dropped presents in the measured route. Rollback
  `--pinyon_shift_fh1_vblank_deadline_wait=false`.
- **Critical-path trace (PERF-11):** default-off
  `--perf_critical_path_trace=true` correlates title emission, PM4
  publication, deferred replay, submission/fence completion, guest vblank and
  present across rotated logs.
- **Opt-in native race pilot (frozen):** see the renderer direction above
  and the [pilot controls](native-renderer/FH1_RENDER_TEST_AUTOMATION.md#native-race-pilot-controls).

### Rejected and unqualified paths

Do not re-enable these from an old roadmap or repeat an unchanged failed
comparison hoping for a better result:

- Scaled owned clears, stencil predication, broad handwritten shader
  substitutions, C347/21B70 terrain and two-UV candidates lack retention.
  Foliage, minimap, map, pause, modal and race regressions are explicit gates.
- B848 native vertex specialization was retained at 2x; its 1x extension and
  geometry-ownership experiment failed performance retention. Captured byte
  parity alone did not justify enabling them.
- Geometry containment, buffer recycling, full-tile ownership and 64 KiB
  invalidation experiments remain off. Bounded copy/consumer proofs do not
  establish streaming, mutation, memory or frame-tail benefit. The historical
  largest-containing lookup could redirect a CPU snapshot while a held GPU
  address still named another owner; overlapping ownership must stay consistent.
- The HUD admission prototype is unretained. The 1x comparison had a +28.87%
  acceleration p99; the 2x comparison stopped on green/white glass and headlight
  artifacts. Later stopped controls remain unexecuted. The production omission
  cause and broad lifetime/visual acceptance are not closed by mipmap work.
- The scaled accumulator presentation experiment is finished and archived at
  tag `experiments/scaled-accumulator-presentation-2026-09-01` (`0e0a42b`).
  It failed qualification, is not for merge, and is not a missing production fix.

## Startup correctness: AUD-01 and AUD-02

- AUD-01: queue signaling and event registration check their HRESULTs separately.
  Fence waits recheck completion after stale wakes/timeouts, check device loss,
  and respect worker cancellation. Failed waits propagate to callers without
  retiring pending submissions; shutdown explicitly drains outstanding work.
- AUD-02: prewarm uses the selected pipeline set, saturates the background-worker
  subtraction, and respects CPU/configured worker limits. Empty selections skip
  work without skipping storage finalization. Thread-creation failure uses the
  remaining workers/processor thread; cancellation stops adding work. An empty
  requested set is distinguished from missing requested pipeline hashes.
- Validation: `python tools/check-fh1-startup.py` compiles the actual production
  methods/selection block with deterministic failure fakes. Release renderer
  build and installed-AppData startup/shutdown pass (session
  `20260910T224536Z-p3184`, exit 0, one scheduled capture, 452 PSOs created).
  Tested renderer SHA256: `C681D4A4660F08A29C4DCDD88547BA54709E08D868A104CD2336D1063F27162E`.
  No gameplay-performance or full device-loss lifecycle claim is made; AUD-03
  remains separate. Raw evidence is local under `.local/aud-01-02/`.

## Non-renderer findings

| Experiment | Decision and evidence |
| --- | --- |
| Unused main registration translation unit | Exclusion is already in `cmake/PinyonShiftRexGlue.cmake`. Runtime registration uses `PPCFuncMappings`; facade entries remain. About 7.19 MB smaller non-PGO executable in the measured builds, plus compile savings; no demonstrated FPS gain. |
| Import tracing | Keep ON. Six matched stationary runs show no consistent runtime/CPU/tail benefit from OFF. |
| ThinLTO / IPO | Keep OFF. Smaller code, but six stationary runs give inconsistent performance. |
| PGO | Keep OFF. Three-scene training and six held-out race runs produced a 2.02% smaller executable without a consistent runtime win. |
| Guest code, SIMD and compiler flag sweeps | Defer until instruction-level hot-path evidence exists. Helper counts and thread CPU totals are not function costs. |
| Asynchronous/coalesced I/O or lookup caches | Defer. In a warm town window, 161 reads totaled 2.471 ms and 47 open/create calls totaled 5.496 ms. This does not characterize cold disks or unrelated metadata operations. |
| Scheduler/polling changes | Defer. Aggregated guest waits establish completed-call coverage, not critical-path causality or OS ready time. Concurrent waits and completion buckets cannot be added to CPU time. |
| Audio/decompression | Defer without a timed hotspot. Absence of observed XMA stalls does not qualify long-play audio. |

Optional default-off I/O and wait diagnostics are preserved as
[experiment patches](../tools/experiments/README.md), not runtime performance
fixes. Their on/off smoke checks passed; broader save/APC and scheduler behavior
is not inferred from them. Evidence remains under `.local/non-renderer-optimization/`.
Cold storage, OS scheduling traces, lower-spec hardware and long-play NPC/audio
timing remain unqualified. No numerical, memory-order, save or I/O policy change
is justified by these experiments.

## Remaining priorities

1. **User-visible regressions:** capture the reported intermittent green rear
   glass frame, complete sustained Carson town/Hot Hatch Hustle comparisons,
   and verify NPC/title UI animation duration against real time. The older
   area report observed roughly 70 FPS in ordinary driving and 15 FPS near
   houses and wooded hills; its exact location was not established. Neither
   that report nor a clean mip frame proves a cause. Preserve the failing frame,
   route, settings and actual binary identity.
2. **Clean installation/artifacts:** the packaged-source, SDK-path, pinned
   Python/CMake and build-error logging fixes are in source. Two empty-cache
   NVIDIA 1x startup runs pass, but the fresh 21,735-variant pack is short of
   the developer 22,012-variant pack. Car-selection variants and full gameplay
   remain gates. Setup does not yet automatically execute the complete artifact
   qualification workflow. Reporter confirmation and a fresh disc-to-game
   installed-launcher run remain required.
3. **Artifact lifecycle:** complete selected-scale production, validated reuse,
   cancellation/resume and atomic activation before claiming setup ready.
   Key DXIL by translator, vendor, flags and scale; key device pipeline warmup
   by exact adapter/driver. Keep production separate from compiler-free runtime.
   Qualify 1x/2x/3x and AMD/Intel on actual hardware; NVIDIA results do not qualify
   those vendors. Follow the [P1 gates](native-renderer/P1_ARTIFACT_PRODUCTION.md).
4. **Renderer:** execute the [Xenos retirement backlog](native-renderer/XENOS_RETIREMENT_BACKLOG.md)
   from XR-00. It replaces the resource-migration checklist (A1–A6 remain
   complete for the 1x owned depth chain; the incremental B items are closed
   in favor of the native backend) and the closed PERF-00–15 program. Manual
   slowdown sites from the September discovery playtest — Horizon Outpost
   entrance and the town plaza (about 44 ms median, 100–113 ms p95) and a
   wooded junction near the Gladstone Canyon sign (155 ms p95) — remain
   useful stress locations. The focused mipmap/cache fixes do not reopen the
   stopped HUD/recycling comparisons automatically.

## Validation and evidence

Use [AGENTS.md](../AGENTS.md) for the installed AppData save launch. Never move,
reset or overwrite saves to manufacture a test. Captured commands, shaders,
memory, generated code and binary artifacts stay local.

Freeze source/SDK/settings, pack/catalog and actual binary hashes for each
comparison; embedded metadata has been stale. Validate process/session identity,
input delivery, source/presentation clocks, scene stage, HUD and motion before
comparing frames. Missing captures, abnormal exits and zero candidate admissions
fail the relevant gate. Keep failed attempts with their explanation.

Use the same save, route, scale/output size and scene state for repeated controls
and candidates. Record median/p95/p99, CPU/GPU time, memory, fallback and removed
work. Put screenshots and profiling outside clean timing windows. The historical
Xenia window-change measurements differ from FH1 source-frame FPS and cannot
establish a product speedup. A whole-route median mixes menus and gameplay.

Check changing contents, partial writes, reuse/destruction, in-flight resources,
queries, memory export, fences and history. Small stable shading differences
need an explicit benefit and motion review; missing geometry, flicker, broken
transparency or simulation timing fail qualification. Cover frontend, garage,
day/night driving, traffic, race, rewind, map, pause, photo, FMV and streaming.

Run [contributor checks](../CONTRIBUTING.md) and the targeted production-body
checks for a changed contract. Summarize performance CSVs with
`python tools/summarize-performance.py <session.perf.csv>`. A faithful dependency
replacement can be retained without an FPS gain if it removes proven work
without material regression. Lower hardware claims require measurements on
that hardware; full retirement requires a clean no-Xenos build and rollback plan.

## Historical evidence

Superseded journals and roadmap versions are retained in Git at the source
checkpoint below. Their “next step”, active-goal and staged-binary statements
are historical. Use this document for current decisions and the focused result
documents for reproduction. Local raw evidence directories are not distributed.

Update the relevant finding/checklist in place. Keep one focused result document
when a retained change needs a reproducible contract; put run-by-run logs and
temporary handoffs under `.local`, rather than adding another roadmap version.

| Archived record | Exact checkpoint |
| --- | --- |
| NON RENDERER OPTIMIZATION RESEARCH | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/NON_RENDERER_OPTIMIZATION_RESEARCH.md) |
| NON RENDERER OPTIMIZATION RESULTS | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/NON_RENDERER_OPTIMIZATION_RESULTS.md) |
| NATIVE RENDERER PERFORMANCE CHECKPOINT 2026-09-04 | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/NATIVE_RENDERER_PERFORMANCE_CHECKPOINT_2026-09-04.md) |
| P2 DEPENDENCY RANKING | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/P2_DEPENDENCY_RANKING.md) |
| B EPIC EXECUTION | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/B_EPIC_EXECUTION.md) |
| NATIVE RENDERER CHECKPOINT 2026-09-10 | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/NATIVE_RENDERER_CHECKPOINT_2026-09-10.md) |
| REPRIORITIZED PLAN | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/REPRIORITIZED_PLAN.md) |
| NATIVE RENDERER V3 BACKLOG | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/NATIVE_RENDERER_V3_BACKLOG.md) |
| NATIVE RENDERER V5 BACKLOG | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/NATIVE_RENDERER_V5_BACKLOG.md) |
| NATIVE RENDERER V6 BACKLOG | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/NATIVE_RENDERER_V6_BACKLOG.md) |
| NATIVE RENDERER BACKLOG | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/NATIVE_RENDERER_BACKLOG.md) |
| XBOX360 NATIVE RENDERER RESEARCH | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/XBOX360_NATIVE_RENDERER_RESEARCH.md) |
| PLAYTEST FEEDBACK 2026-09-07 | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/PLAYTEST_FEEDBACK_2026-09-07.md) |
| 2026-09-07-area-performance-drop | [View record](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/screenshots/2026-09-07-area-performance-drop.png) |
| Scene-native, Rayman and performance backlogs, the resource migration checklist and their 2026-09-08–27 journals (`RAYMAN_*`, `SCENE_NATIVE_*`, `PERFORMANCE_*`, `CPU_HOTSPOT_RESULTS_*`, discovery findings, Skate milestones, C1/C2 profile) | [View directory](https://github.com/arcanite24/pinyon-shift/tree/02dfad07fc1236625520a948bb5cd2afe74bfbb3/docs/native-renderer) — resolves once `dev` is pushed; locally `git show 02dfad0:<path>` |
