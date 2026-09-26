# Textured UI replay pilot — 2026-09-25

The selected race frame now replays all 166 captured HUD draws into the
native D3D12 output: 116 untextured draws and 50 draws using one or two
source-pinned textures. The resulting image has readable lap/time/place text,
leaderboard, minimap and speedometer over the existing native scene. The
route exited normally without a D3D12 device removal. This is an offline
single-frame pilot, not a live native race admission.

The shader pack already contained the three original UI shader pairs. The
two textured pixel programs use a bindless 2D-array table and sampler table;
the pilot binds their captured fetch constants, descriptor indices and
source-draw texture versions. The SDK retains the source frame's texture
snapshots through the next output frame, where the replay consumes them.
The capture verifier reports 166 contiguous, final-state-complete draws,
58 texture bindings, 16 unique texture versions, and zero outdated versions.

Run with the AppData preview save and
`config/render-tests/fh1-native-race-profile.fh1test`'s race sequence via
`.local/native-frame-seam-20260925/ui-replay-capture.fh1test`:

```powershell
.\tools\build-preview.ps1 -Configuration RelWithDebInfo -Parallel 8
$stateRoot = Join-Path $env:LOCALAPPDATA 'PinyonShift\source\0.1.0\.local\preview'
.\tools\launch-preview.ps1 -Configuration RelWithDebInfo -StateRoot $stateRoot `
  -RenderTestScript .local/native-frame-seam-20260925/ui-replay-capture.fh1test `
  -RenderTestOutput .local/ray-ui-textured-pilot-c-20260925 `
  -RenderTestTimeoutSeconds 900 -Hidden `
  -GameArgumentsJson '["--pinyon_shift_native_race=false","--pinyon_shift_native_race_capture_start_frame=6500","--pinyon_shift_snr01_trace_source_frame=6803","--pinyon_shift_native_ui_replay_source_frame=6803","--log_level=warn"]' -Json
python tools/verify-ordered-ui-capture.py `
  .local/ray-ui-textured-pilot-c-20260925/ordered-ui-6803.bin
```

The successful output is
`.local/ray-ui-textured-pilot-c-20260925/native-ui-pilot-late.png`.
Two immediately preceding repeats reached the same selected source frame
without any UI producer draws, so the compatibility frame was retained.
The successful run had all 166. This confirms an intermittent producer or
frame-scheduling gap, not a texture-version miss. Live replay must account
for reused UI output or fix that scheduling gap before takeover. The native
world/car remain visibly approximate, and this pilot still draws its scene
from the existing scene snapshot rather than the complete ordered event
stream. The experimental admission path must preflight both passes before
it can satisfy the whole-frame fallback contract.
