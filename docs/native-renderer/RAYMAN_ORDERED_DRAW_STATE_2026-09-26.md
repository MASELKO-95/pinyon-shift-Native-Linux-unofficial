# Ordered draw state and retained UI, 2026-09-26

The opt-in ordered-frame CSV now records each draw's shader variants,
primitive and index input, prepared and final depth state, color mask,
raster/clip state, viewport, scissor, and every final texture-version key
(fetch words, allocation ID, payload generation and outdated mask). Float
state is written with nine significant digits. This extends the selected
consumed-command stream without changing production native presentation.

The capture also records the UI replay source. When a selected frame has no
UI producer and no intervening UI-target write, it exports the complete
retained UI fixture from the preceding frame. The ordered-frame verifier
checks draw state, texture-key encoding and the referenced fixture's
existence; the UI verifier checks the fixture's owned inputs.

The RelWithDebInfo build and saved AppData race route exited normally. In
`.local/ray-ui-native-promotion-20260925/ordered-frame-retained-ui-output`, source
frame 5000 has 3,244 ordered events: 3,126 draws, 97 copies and 21 clears.
All draws have final state, including 5,629 texture keys; no copy failed.
The 48 ordinal gaps are the previously classified mip skips. This run had
zero UI producer draws in source frame 5000, and its CSV points to retained
source frame 4999. `ordered-ui-4999.bin` verifies as 166 complete UI draws
with 16 unique texture versions. The live log promoted output frame 5001
using UI frame 4999.

RAY-00/01 remain open: the CSV identifies world inputs but does not yet own
all world vertex/index bytes, texture pixels or intermediate render-target
versions. Clears and resolves are ordered but are not yet replayed from this
stream into one offline full-frame image.
