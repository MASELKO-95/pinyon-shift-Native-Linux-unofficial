# Native UI replay pilot

RAY-01 progress, 2026-09-25. A render-test-only native output path now combines
the existing owned race scene with UI draws from the **same source frame**. It
uses the original packed vertex and pixel shaders for the most common
untextured HUD pair. The completed UI capture is retained through the scene's
later output boundary; source frame 6803 was presented at output frame 6804.
The normal native race toggle and compatibility fallback are unchanged.

The short race route at
`.local/native-frame-seam-20260925/ui-replay-capture.fh1test` ran with
`--pinyon_shift_snr01_trace_source_frame=6803`,
`--pinyon_shift_native_ui_replay_source_frame=6803`, and
`--pinyon_shift_native_race_capture_start_frame=6500`. The RelWithDebInfo
preview exited normally. Its selected UI binary has 166 ordered draws, no
missing final states or sequence gaps, and 116 untextured draws eligible for
the pilot. The other 50 draws use texture bindings and are skipped. The
verifier also checks guest index and vertex-fetch bounds for the 116 draws.

The late screenshot at
`.local/ray-ui-pilot-index-20260925/native-ui-pilot-late.ppm` (SHA-256
`7A838AAB643E5329BBE427536CE522A3018618DBB712D71FA25C0E1E6C1BAA57`)
shows the native road/car and readable lap, place, timing, leaderboard and
speed digits. The minimap, gauge artwork and colored UI highlights are absent.
The native world is still visibly rough. This is a recognizable pilot image,
**not** the complete offline RAY-01 result or a usable native renderer.

Two address mistakes were found through the image check. Rebasing the vertex
fetch in both the buffer address and fetch constant produced a black frame
and a GPU hang; rebasing it once ended the hang. Swapping the index bytes in
the upload and again in the original vertex shader scrambled HUD geometry;
preserving guest bytes made text legible. The corrected route exited normally.

An earlier run targeting source frame 6801 emitted no UI binary, even though
the same scripted frame had a complete 166-draw capture in another run. The
complete-frame guard showed compatibility output. This confirms the UI
producer/scheduling gap remains intermittent and must be solved before live
takeover. The next replay slice should capture and bind the two textured HUD
shader pairs with immutable texture pixels, then use the ordered frame stream
for targets, clears and resolves instead of this scene-plus-UI pilot.
