# Ordered draw-dispatch pilot — 2026-09-26

The selected-frame native shadow path now reads the authoritative GPU event
stream and dispatches every supported six-family world draw in that order.
It rejects a missing or mismatched stream before drawing and leaves other
frames on the existing native path. The stream's unsupported draws, clears
and copies are counted, not yet replayed. Visible output still uses the
existing whole-frame compatibility fallback when native race is off.

The scene snapshot's `source_frame` names `output_frame - 1`, while prepared
GPU events retain `output_frame`. Joining both records on the same numeric
frame gave zero matching ordinals; joining event frame 5001 to scene frame
5000 matched all 2,124 captured six-family draws. The remaining stream has
1,989 draws outside those families, 97 copies, and 21 clears. Its 4,113 draws
have complete owned geometry, final-bound state, and 431/431 exact pinned
texture versions. The same run captured 180 UI draws.

The saved `native-shadow-5001.ppm` is a recognizable race frame with road,
car, scenery, lap/place HUD and speedometer. It still has the known approximate
materials and missing world effects. This proves the ordered dispatch join;
it does not establish full ordered target/copy replay or original shader
execution. The image and strict capture artifact are under
`.local/ray-ui-native-promotion-20260925/ordered-dispatch-correct-output/`.

Reproduce with the **RelWithDebInfo** preview binary; the default Release
binary can be older and will silently skip this pilot. Use
`manager-rejection-short.fh1test`, `-StateRoot` set to the installed preview
save root, and these game arguments:

```text
--pinyon_shift_native_race=false
--pinyon_shift_native_race_capture_start_frame=4992
--pinyon_shift_native_ui_live=true
--pinyon_shift_native_ui_shadow_start_frame=4992
--pinyon_shift_snr01_trace_source_frame=5001
--log_level=warn
```

Validation: `tools/verify-ordered-frame.py ordered-frame-5001.csv
--require-owned-inputs` passed (4,113/4,113 geometry and state; 431/431
textures; zero missing final draws). `tools/verify-ordered-ui-capture.py
ordered-ui-5001.bin` passed. The render test exited normally and the shadow
image was visually inspected.

Next: execute target changes, clears and copies in the same event loop, then
bring the remaining draws and UI into that loop. Keep the pilot in shadow
until a complete saved frame is coherent.
