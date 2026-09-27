# Second initial-color version: paired comparison and consumer — 2026-09-26

The selected-frame stream resolves guest color base `484626432` twice.
Its feedback draw samples the first pinned version; later car-body draws
sample the second at fetch constant 13. A temporary scene-only diagnostic
sampled the native second version and that exact pinned guest second version
in one frame, displaying each at half width. In the central region
`y=230:450`, 137,058 of 140,800 paired pixels (97.342%) matched exactly;
mean absolute RGB error was `(1.017, 0.378, 0.051)` on 8-bit output.
This compares every other source column and excludes the overlaid HUD.
The paired image is
`.local/ray-ui-native-promotion-20260925/ordered-feedback-paired-preview-3-output/native-shadow-5001.png`.
It proves a close match for this resolved texture, **not** a correct whole
frame: both sides still show the same strong yellow/green color error.

After removing the diagnostic shader, selected-frame replay identifies the
pinned second guest version from the first car-body draw and replaces one
matching car-material descriptor with the native second version. The native
resource transitions to shader-read state at its second copy ordinal. If the
expected texture or consumer is absent, replay rejects the selected frame
and the existing whole-frame compatibility fallback remains available.

The RelWithDebInfo short race exited normally. The log recorded
`FH1 RAY01 native second color consumer frame=5001 sequence=11138581`,
both initial color copies and all twelve offscreen draws. The saved
`ordered-feedback-consumer-output/native-shadow-5001.png` is readable and
has no new obvious car defect. The ordered verifier passed 2,895 owned
draws, 97 copies and 21 clears; retained source-5000 UI verification passed
all 166 draws. This pilot routes only one car draw. Other consumers and the
first native color version still use the guest texture.
