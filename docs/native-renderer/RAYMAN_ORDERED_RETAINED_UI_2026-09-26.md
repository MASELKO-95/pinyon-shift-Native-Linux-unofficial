# Ordered replay across a retained HUD frame — 2026-09-26

The selected ordered replay can now use the immediately prior complete HUD
draw set when its current frame has no HUD producer. It requires the previous
and current event streams to end in successful full-size copies from the
same surface, depth and EDRAM source shape. It also rejects any observed
write to the captured HUD target in the current stream. The copy destination
address may differ: the live cadence capture showed the two guest output
buffers alternating between `471109632` and `467341312` while source base,
format, pitch and control remained the same. The complete native frame still
falls back if any admission check fails.

The shadow-window capture now retains the terminal full-size copy alongside
the prior HUD draws, so this comparison has actual prior-frame data. In the
UI admission stress route, selected frame 6782 had zero HUD draws and named
replay source 6781. The runtime logged `ordered retained HUD admitted`,
ordered dispatch of 1,837 supported draws, and full native promotion with
`ui_frame=6781`. The route exited normally. The strict frame verifier passed
3,178 draws, 97 copies, 21 clears, complete geometry/state and texture pins;
the UI verifier passed all 166 draws in source frame 6781. Evidence is under
`.local/ray-ui-native-promotion-20260925/ordered-retained-rebase-output/`.

The selected trace frame now requests one render-test image readback, even
outside the usual shadow image window. A later frame-6782 run with its own
HUD producer saved `native-shadow-6782.ppm`: the race and HUD are readable,
while large yellow background artifacts remain. That image is evidence for
the same-frame path only; the retained-HUD promotion was verified by its
runtime log and capture artifacts, not by a saved image from that exact run.
The guard covers one retained frame and the observed target/copy shape. A
longer drive and visible-gap work remain before playability is established.
