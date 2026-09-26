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
5000 matched all 2,477 captured six-family draws in the final run. The
remaining stream has 1,796 draws outside those families, 94 copies, and 20
clears. Its 4,273 draws have complete owned geometry, final-bound state, and
482/482 exact pinned texture versions. The same run captured 166 UI draws.
The selected-frame shadow replay takes its UI from frame 5001 too and rejects
the pilot if that same-frame UI capture is unavailable.

The saved `native-shadow-5001.ppm` is a recognizable race frame with road,
car, scenery, lap/place HUD and speedometer. It still has the known approximate
materials and missing world effects. This proves the ordered dispatch join;
it does not establish full ordered target/copy replay or original shader
execution. The image and strict capture artifact are under
`.local/ray-ui-native-promotion-20260925/ordered-dispatch-same-frame-output/`.

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
--require-owned-inputs` passed (4,273/4,273 geometry and state; 482/482
textures; zero missing final draws). `tools/verify-ordered-ui-capture.py
ordered-ui-5001.bin` passed. The render test exited normally and the shadow
image was visually inspected.

An opt-in repeat with `--pinyon_shift_native_race=true` also exited normally.
Its output-5001 log recorded `ui_frame=5001` and `promoted=true`; the saved
`manager-frame.ppm` visibly contains the native road, car, scenery and HUD.
That checks selected-frame presentation, not continuous complete-stream
replay.

The selected-frame path now verifies that every captured UI draw has the same
ordinal and target as its event-stream draw, with no other draws or target
operations interleaved before the UI suffix ends. A repeat opt-in route under
`.local/ray-ui-native-promotion-20260925/ordered-ui-suffix-output/` promoted
frame 5001 and exited normally. Its strict capture verified 2,903/2,903
owned geometry/state draws and 410/410 texture pins; all 170 UI draws were
in the same event stream. This ties the existing separate UI draw routine to
the authoritative order for this selected frame; intermediate targets and
copies are still absent from native execution.

The selected-frame replay also recognizes the regular main-scene clear when
it immediately precedes the first supported draw and matches its surface,
depth, tile bounds and EDRAM base. It applies the captured color and depth
values to the native scene at that ordinal. A run under
`.local/ray-ui-native-promotion-20260925/ordered-main-clear-probe-output/`
logged clear ordinal 11340416 with a 1280×256 rectangle, promoted output
5001, exited normally, and saved a visually readable native frame. Its strict
capture verified 2,918/2,918 geometry/state draws and 419/419 texture pins.
This executes one of 20 captured clears; the other clears, all 94 copies,
target aliases, and stencil handling remain open. A separate source-5001
no-producer run rejected selected-frame takeover and retained compatibility.

The selected-frame takeover now requires one terminal successful 1280×720
copy from the UI surface, after the last UI draw, with a concrete guest
destination. The existing native scratch-to-output copy is the presentation
operation for this selected event; earlier guest-visible copies are still
performed by the compatibility renderer. An opt-in route under
`.local/ray-ui-native-promotion-20260925/ordered-final-copy-output/` ended
with 167 UI draws followed by copy ordinal 11301438 to destination 471109632.
The log confirmed the main clear and native promotion; the saved frame was
visually readable. Strict verification passed for all 3,006 owned draws and
417/417 texture pins, and the UI capture verifier passed.

Next: execute target changes, clears and copies in the same event loop, then
bring the remaining draws and UI into that loop. Keep the pilot in shadow
until a complete saved frame is coherent.
