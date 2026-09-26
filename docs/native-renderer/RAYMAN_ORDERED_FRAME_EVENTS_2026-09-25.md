# Ordered race-frame events

RAY-00 progress, 2026-09-25. The selected consumed GPU seam now emits one
ordinal-sorted CSV containing prepared draws, successful optimized rectangle
clears, and copy/resolve operations for a selected source frame. Clear records
include up to two rectangles, depth/color values, stencil reference, and the
regular/owned-depth/owned-tile mode. The UI binary captures owned input bytes
and final texture identities for the same selected frame. Both are opt-in
diagnostics under `--pinyon_shift_snr01_trace_source_frame=N`.

RelWithDebInfo build and short render-test runs exited normally with the
installed preview save. `python tools/verify-ordered-frame.py` checked sorted,
unique ordinals, clear records and final state; the UI verifier checked every
saved vertex/index buffer hash. Local evidence is under
`.local/ray-frame-clear-20260925/` and
`.local/ray-frame-native-fixed-20260925/`.

| Mode / source frame | Regular draws | Clears | Copies | UI draws | Missing final states | Internal gaps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Compatibility / 6801 | 4,352 | 20 | 95 | 166 | 0 | 48 |
| Native scene capture / 6803 | 4,023 | 21 | 97 | 180 | 0 | 48 |

The first frame has 16 regular-target and four owned-depth clears. Its 48
unrepresented ordinals exactly match `fh1_mip_replacement_active_` skips,
which record no draw. An exit trace confirmed that prepared draws without a
regular final-state callback were the optimized clears; these now have their
own event records. The native-scene-capture frame's UI binary verifies all
180 draws, three shader pairs plus one additional pair, 72,956,664 vertex
bytes, 133,044 index bytes, and 60 versioned texture bindings. Its separate
UI snapshot budget prevents unrelated scene probes from exhausting capture.
The UI artifact hashes its owned bytes with FNV-1a independently of the
SDK's mode-dependent snapshot hash, so one verifier works in both modes.

The earlier sample's HUD-free source frame 6803 was not reproducible in these
new runs: both compatibility and native-scene-capture routes emitted UI draws,
and the inspected native-capture `scene-6802.ppm` shows a readable HUD. The
earlier observation remains a scheduling risk to test during live shadow;
these runs do not establish that it is fixed.

A later [UI replay pilot](RAYMAN_UI_REPLAY_PILOT_2026-09-25.md) did reproduce
the gap: one source-6801 run wrote no UI binary, while adjacent runs captured
complete UI. The whole-frame guard kept compatibility output in that run.

**Still needed for RAY-00/RAY-01:** the frame CSV carries shader/target IDs
and texture-version counts, not all vertex/index bytes, texture identities,
pixel contents, or intermediate render-target versions. Only UI has the owned
payload binary. Feed those immutable inputs and the ordered clears/resolves
into the existing D3D12 output to produce an offline race image before
claiming RAY-00 or RAY-01 complete.
