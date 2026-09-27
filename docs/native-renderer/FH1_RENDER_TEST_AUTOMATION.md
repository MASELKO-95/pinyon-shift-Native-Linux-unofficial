# FH1 renderer test automation

This is a Forza Horizon 1-only unattended test path. It launches the real game
and native renderer with a synthetic controller, writes full-resolution PPM
captures, and exits through the normal window-close path. Scripts use completed
guest-output frames by default; `# clock-hz N` makes their frame numbers an
explicit wall-time clock for stock-versus-unlocked comparisons. It does not use
computer use, screen scraping, or a physical controller.

For the installed AppData save, follow [AGENTS.md](../../AGENTS.md): verify its
`user/**/ForzaProfile/ForzaProfile` exists and no `pinyon_shift` process is running,
then launch through `tools/launch-preview.ps1` with that preview as `-StateRoot`.
Do not copy or reset this save for testing. Scripted/capture launches intentionally
skip automatic shader-pack and prewarm staging, so explicitly stage the selected
pack before a comparison. For an existing prepared 1x state:

```powershell
$stateRoot = Join-Path $env:LOCALAPPDATA 'PinyonShift\source\0.1.0\.local\preview'
python tools/native-shader-pack.py stage `
  .local/native-renderer/fh1-disc-aot-complete-1x.pnsp --state-root $stateRoot --scale 1
if ($LASTEXITCODE) { throw 'Shader pack staging failed.' }
.\tools\launch-preview.ps1 -StateRoot $stateRoot `
  -RenderTestScript config/render-tests/fh1-map.fh1test `
  -RenderTestOutput .local/native-renderer/map-test `
  -GameArguments @('--draw_resolution_scale_x=1', '--draw_resolution_scale_y=1')
```

Pin pack and native shader/pipeline/prewarm catalog hashes before every condition,
check them again after exit, and verify the loaded pack count in the session log.
Do not silently replace a pack midway through a comparison. The direct launcher
produces captures/logs; run the appropriate clock, workload and image checks
separately before accepting the run. In-game autosaves remain normal gameplay.

The separate `tools/run-fh1-render-test.py` runner supports disposable test seeds
where copying is permitted; it is not the AppData-save procedure above. Each
runner invocation copies only `user` and `config` from its seed into a private
sibling directory beside its output. FH1 may autosave in the private copy, but
the selected seed is never written. Cache contents
are deliberately not shared between runs. `--seed-shader-storage`
`--seed-pipeline-prewarm` copies the immutable FH1 native shader/pipeline catalog
and its allowlist; it no longer seeds writable `.xsh`/`.xpso` stores.
Supplying `--shader-pack` automatically seeds the catalog because the shipping
backend needs both pieces.

Pass `--baseline-dir <previous-output>` to compare each capture named by an
`# expect-image` line with a known-good run. Those lines set limits for mean
absolute error, root-mean-square error, and changed-pixel ratio. Dynamic race
and free-roam shots intentionally allow traffic, camera, and simulation drift;
the static SELECT map has a tight limit. Baselines contain game imagery and
therefore remain local rather than being committed. Creating such a reference
requires the explicit `--record-baseline` switch; a scenario with image
expectations otherwise fails before launch, preventing a missed reference from
being reported as a successful visual gate.

`# expect-performance` sets maximum median frame time, minimum presentation
rate, and the permitted main-loop-rate range. The legacy telemetry field is
named `simulation_tick_count`, but the measured hook is the FH1 application
loop and must not be interpreted as an individual physics-step counter.
`# require-native` requires an
exact FH1 native family to execute. A run also fails for missing or wrong-frame
captures, blank output, renderer/GPU/device-loss errors, a missed capture frame,
or an abnormal process exit. The PowerShell launcher owns the exact child PID
and terminates it if the render-test timeout expires.

`# expect-distinct-presentation <minimum-hz>` rejects repeated host presents;
only distinct completed FH1 frames count. Use repeatable
`--game-argument=<cvar>` options for cadence and resolution qualification. The
launcher serializes the list so multiple PowerShell options cannot be mistaken
for launcher parameters.

`# expect-capture-mae <first> <second> <minimum>` proves that a scripted mode
transition actually happened before a baseline can pass. The map scenarios use
it to reject tutorial profiles where SELECT is intentionally unavailable.

Add `--collect-pass-inventory` for a dedicated run that records the ranked
exact FH1 pass-family identities, draw ranges, samples, and measured GPU
nanoseconds in `result.json`. These are the compatibility-cost inventory used
to choose or reject further V5 retirements; native-family counts separately
prove work actually removed. Normal performance runs leave this instrumentation
off so its per-draw timing does not contaminate frame measurements.

The committed scenarios cover:

- `fh1-smoke.fh1test`: launch, capture, and clean self-termination;
- `fh1-fmv.fh1test`: startup/FMV composition with
  `--include-opening-movies`;
- `fh1-free-roam.fh1test`: driving view, HUD, and minimap;
- `fh1-map.fh1test`: SELECT map roads/icons and return to free roam; and
- `fh1-pause.fh1test`: pause overlay and return to free roam; and
- `fh1-photo-mode.fh1test`: enter and leave FH1 photo mode; and
- `fh1-source-60.fh1test`: sustained driving with a real distinct-frame gate;
  and
- `fh1-hfr-modes-control.fh1test` / `fh1-hfr-modes-unlocked.fh1test`: paired
  wall-time free-roam, SELECT-map, pause, and resume qualification; and
- `fh1-race.fh1test`: event entry, car/start menus, live race HUD, and motion
  without completing the event.

This is not a synthetic renderer unit test and does not bypass the real FH1
runtime. A normal host window may exist while the run is unattended; hiding or
reimplementing it adds no test reliability. Map and race traces still require a
known progressed local seed at their expected location. The runner copies that
seed before launch; unknown or locked scene state fails before it can become a
visual baseline.

## Native race pilot controls

These drive the frozen six-family race pilot described in the
[Xenos retirement backlog](XENOS_RETIREMENT_BACKLOG.md). They apply only to
an unpaused race; every other mode stays on the Xenos renderer. Pass them
with `-GameArguments`:

| Setting | Effect |
| --- | --- |
| `--pinyon_shift_native_race=true` | Native race output; hot-reloadable master switch |
| `--pinyon_shift_native_ui_live=true` | Replays the race HUD and promotes complete native frames; needed for continuous output |
| `--pinyon_shift_native_race_capture_start_frame=N` | First captured source frame; 1 when unset and native race is on |
| `--pinyon_shift_native_ordered_live_probe=true` | Diagnostic: 64-frame rolling ordered draw/copy/clear capture |
| `--pinyon_shift_snr01_trace_source_frame=N` | Diagnostic: ordered frame CSV, UI/state artifacts and selected-frame replay for frame N |
| `--pinyon_shift_native_ui_replay_source_frame=N` | Diagnostic: one-frame ordered HUD replay pilot (render tests only) |
| `--pinyon_shift_native_ui_shadow_start_frame=N` | Diagnostic: 24-frame shadow pilot starting at N |
| `--pinyon_shift_native_small_target_probe=1..8` | Diagnostic shadow probe: 1–3 native and 4–6 guest reduction targets, 7/8 guest/native scene |
| `--perf_critical_path_trace=true` | Correlated title/PM4/submission/present trace; also logs native stage timings |
| `PINYON_SHIFT_SNR04_RENDERDOC_TRIGGER_FILE` (environment) | Triggers a same-output RenderDoc capture |

A script can switch native output at an output frame with
`native-race <frame> <true|false>`; frames must increase.

| Route | Purpose |
| --- | --- |
| `fh1-native-race-profile` | Race start and a short moving window for timing |
| `fh1-native-race-output-stability` | Output-paced race to output frame 6920 |
| `fh1-native-race-toggle`, `fh1-native-race-hot-toggle` | Native/Xenos/native switching |
| `fh1-native-race-mode-boundary` | Race → pause → free roam → title hand-back |
| `fh1-native-ui-admission-stress` | Dense HUD admission and no-UI-producer frames |
| `fh1-native-scene-continuous`, `fh1-native-scene-exact`, `fh1-native-output-adjacent`, `fh1-snr04-adjacent`, `fh1-snr02-title-reload` | Earlier scene-capture and handoff checks |

Verifiers: `verify-native-race-mode-boundary.py` and
`verify-native-race-toggle.py` (they detect native frames by the pilot's
flat sky color and need replacing, see XR-00), `verify-ordered-frame.py`
and `verify-ordered-ui-capture.py` (ordered-capture artifacts), and
`verify-native-output-seam.py`, `verify-native-scene-handoff.py`,
`verify-native-track-output.py` and `verify-native-ui-clear-probe.py` for
the earlier probes. The routes use the AppData save; its progress now
reaches a different event than Recaro Rush, so XR-00 moves them to pinned
disposable seeds.
