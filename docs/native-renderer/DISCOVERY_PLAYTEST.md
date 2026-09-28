# Manual discovery sessions

Run `tools/start-fh1-discovery.ps1` to play normally with the native renderer
and the installed AppData save. It uses `tools/launch-preview.ps1`, checks for an
existing game process and a valid profile, and does not copy or reset saves.
No scripted inputs or automatic game exit are used in a normal session.
Coverage observes one complete source frame in 60, aligned with the existing
sampled pass/texture timings. Per-frame CSV measurement continues on all frames.
Brief effects can fall between coverage samples; repeated visits help. Coverage
counts are sample counts and must not be reported as whole-session totals.

Since the Xenos renderer was removed (`6b75238`), nothing feeds the GPU
corpus or the sampled pass timings: they came from the Xenos draw and copy
observers, which the native renderer does not have. On the current build
the coverage files and pass records stay empty and the
`fh1_discovery_sampling` setting no longer exists. Frame windows, process
samples, markers and screenshots are unaffected. The corpus and pass
descriptions below document the recorder's formats and the earlier
sessions.

- **Ctrl+Shift+F8:** mark a slowdown.
- **Ctrl+Shift+F9:** mark a visual or animation/timing problem.
- A system notification sound acknowledges a marker. Hotkeys only create markers
  while the game is foreground. Close the game normally when finished.
- Screenshots require Pillow in the selected `-PythonExe`. Without it, timestamp
  markers still work and the report explicitly says screenshots are disabled.
  The current desktop session uses the already installed Codex Python runtime.

Visit different towns, countryside, races, cars, camera views and menus. For a
slowdown, mark the approach, linger/change the camera, leave, then return. Mark
animation problems while the affected NPC or transition is visible. Short notes
about the location and what looked wrong remain useful; the recorder cannot
recognize visual correctness or automatically determine the map location.

## Collected evidence

Each launch creates `.local/native-renderer/discovery/<timestamp>/` containing:

- `build.json`, `settings.toml`, `session.json`: binary hashes, settings and exact
  runtime session/CSV path. Existing session CSV files remain in AppData logs.
- `windows.jsonl`, `report.md`, `report.json`: ten-second frame windows, median,
  p95/p99, sampled GPU time and resource activity; slow windows ranked for review.
- `process.jsonl`: process CPU seconds, working set and private bytes once/second.
- `markers.jsonl`, `marker-*.png`: problem markers and game-window screenshots.
- `samples.log`: current-session FH1 timing/event records preserved across runtime
  log rotation. Existing ranking tools can consume these using the exact session ID.
  Pass records also include `recording_wall_ns`, `begin_submission` and
  `end_submission`: host wall time spent recording the span and its submission
  boundaries. Wall time includes waiting/preemption and is not CPU utilization.
  It is not calibrated to GPU timestamps and cannot be subtracted to compute GPU
  busy time. The existing ranker reports recording totals and spans crossing
  submissions when these fields are present; older logs remain supported.
- `coverage.jsonl`, `coverage-ranking.json`: cumulative shader-pair coverage and
  pairs first seen since the preceding checkpoint. This is novelty within the
  session, not a comparison against every previous playtest.
- `recorder-stats.json`: helper CPU time and archive sizes, updated every 30 seconds.

The game writes a cumulative corpus snapshot every five minutes and on normal
shutdown. The newest snapshot replaces the same session file under
`cache/fh1-gpu-corpus`; a crash can lose coverage since the last checkpoint.
Periodic snapshots do not finish/split the active pass or turn observation off.
Both draw-key and pass maps are bounded at 65,536 entries. Overflow/collision
reports mean coverage is incomplete; never treat these as full-game qualification.
Shader-family coverage now uses a separate map of up to 4,096 vertex/pixel shader
pairs. It keeps counting known pairs and discovering new pairs after the detailed
key inventory fills. If the family map itself fills, its own overflow counter
marks that inventory incomplete. These pairs do not distinguish specialization
variants or establish resource/pass correctness.

Reports distinguish the latest family inventory from detailed-key completeness.
An unreadable/invalid snapshot replaces the current ranking with an explicit
unavailable status, rather than continuing to present an old ranking as current.
Older recordings lack the independent map and still require complete detailed
keys to derive a reliable family ranking; missing past observations cannot be
recovered by this change. The strict detailed-key ranking is unchanged; use
`rank-fh1-gpu-corpus.py --families <snapshot>` for the independent family view.

Reports update every 30 seconds and on game exit. JSONL streams and archived
samples flush during recording. Markers include UTC and the latest CSV row/time;
one-second polling plus the writer's 60-frame flush introduces association delay.
Use surrounding rows, not the marker's row as an exact GPU event.
The launcher snapshots existing CSV filenames before starting the game, so a
reused Windows process ID cannot select an older session's measurements.

## Limits and interpretation

The per-session CSV stops at 512 MiB (checked every 60 frames, so a small overshoot
is possible). The archived sample log caps at 256 MiB. Screenshots stop at 100
images or after crossing 256 MiB; at most one image may exceed that threshold.
Up to 1,000 timestamp markers are retained. Runtime logs use the existing 5 MiB /
20-file rotation. The helper stops after 12 hours; it never terminates the game.
Limits apply per session; old sessions are not automatically deleted.

Detailed scene dumps and RenderDoc capture are disabled. Even so, periodic
snapshot writes and screenshots can perturb timing. The overhead figures in
the rest of this paragraph were measured on the Xenos renderer.
The initial full-observation pilot increased median frame time by 26.34% with
1.40% more draws; full observation was therefore rejected for the manual launcher.
Discovery sampling leaves native rendering admission and guest writes unchanged;
it gates diagnostic draw/copy observers and their CPU stopwatch on unsampled frames.
The follow-up sampled pilot measured median +3.18%, p95 +8.37%, p99 +7.70%,
GPU +3.35% and draws/frame +0.73% against the preceding off run (32–44 seconds).
These are single-run pilot observations, not repeated overhead qualification.
The helper used 1.20 CPU seconds over its first 30.52 wall seconds, including
startup. Evidence: `.local/native-renderer/discovery-overhead-summary.json`.
This mode discovers useful reproductions; confirm optimization gains with matched
uninstrumented runs. A pass's first shader is not its isolated GPU cost, and draw
coverage is not proof that a dependency has been retired. A marked screenshot
cannot establish animation speed; follow up with motion/timing comparison.

## Checks

`python tools/record-fh1-discovery.py --self-test` checks partial-line handling,
rotation identity rejection, frame-window calculations and report serialization.
The ranking tests cover detailed overflow, family overflow, duplicate
families, legacy rejection and successive checkpoint novelty. The compiled
CSV-writer and family-coverage checks (`check-fh1-discovery-csv.py`,
`check-fh1-family-coverage.py`) were removed with the other Xenos-era source
checks in `327be88`.

The automated smoke option `-RenderTestScript <script> -CheckpointSeconds 5`
is for developer validation only. It uses the existing render-test runner and
closes the game according to that script; do not use it for manual discovery.
