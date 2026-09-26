# Final-bound inputs for the ordered race frame, 2026-09-26

The selected RAY-00 stream now exports `RAYSTA01`, one state record for every
ordered draw. Each record preserves the final draw's 512 float4 registers,
vertex and pixel constant bitmaps, 64 system words, 192 fetch words, 40
bool/loop words, and dynamic-state key. Capture happens at the final draw
observer, after the emulator binds the state. Non-selected frames do not
retain these large arrays.

The RelWithDebInfo build and saved AppData race route exited normally. The
output is `.local/ray-ui-native-promotion-20260925/ordered-state-output`.
Source frame 5000 has 3,079 events: 2,965 draws, 94 copies and 20 clears.
`ordered-state-5000.bin` is 28,048,920 bytes. Strict verification found a
state record and owned geometry for every draw, and an exact GPU pin for
every one of the 414 texture versions in the stream. The separate HUD
fixture has 166 complete draws.

The installed 1x D3D12 shader pack has 24,763 entries. An index lookup of
the selected stream found all 179 vertex/pixel specialization pairs needed
by its 2,965 draws; no shader variant was missing. This checks identity
availability, not whether all corresponding native pipelines will create
or render correctly.

`verify-ordered-frame.py ordered-frame-5000.csv --require-owned-inputs`
passes and checks the state artifact's frame, draw count, order, status and
record bounds. `verify-ordered-ui-capture.py ordered-ui-5000.bin` passes.
RAY-01 remains open: these inputs have not yet driven the ordered draws,
clears and resolves through native targets into a saved full-frame image.
