# Ordered copy inputs — 2026-09-26

The ordered frame stream now retains the copy-source selection, EDRAM base,
pitch, format and MSAA mode; source-resource dimensions; guest and physical
source rectangles; destination offset, pitch and height; sample selection;
and validity/availability flags. These fields come from the already observed
`GraphicsCopyObservation` and are available in both
`SnapshotOrderedFrameOperations()` and `ordered-frame-N.csv`. Previously the
stream had only destination base/pitch and resolve width/height, which could
not distinguish a color copy from a depth copy.

The RelWithDebInfo continuous race run captured output frame 5001 with
3,277 ordered events: 3,159 draws, 97 copies and 21 clears. The strict
ordered verifier passed with all draw geometry/state owned and 404 exact
texture versions pinned. All 91 copies with nonzero dimensions had valid,
available source targets and in-bounds source/destination rectangles. Six
zero-size copy events are the already known mip-skip ordinals. The route
exited normally. This particular frame had no UI producer, so the selected
native takeover correctly yielded; the copy evidence does not claim a new
visible native frame.

The new source fields correct the main intermediate dependency:

| Copy role | Source select / EDRAM base | Destination | Extent |
| --- | --- | --- | --- |
| Initial depth | depth (4), base 0 | `497831936` | 1280×720 |
| Initial color | color (0), base 720 | `484626432` | 1280×720 |
| Main tile 1 | depth (4), base 1024 | `497831936` | 1280×256 |
| Main tile 1 | color (0), base 0 | `474877952` | 1280×256 |
| Main tile 2 | depth (4), base 1024 | `499142656` | 1280×256 |
| Main tile 2 | color (0), base 0 | `476188672` | 1280×256 |
| Main tile 3 | depth (4), base 1024 | `500453376` | 1280×208 |
| Main tile 3 | color (0), base 0 | `477499392` | 1280×208 |
| Final presentation | color (0), base 0 | `471109632` | 1280×720 |

The three tile destinations in each column are consecutive sections of a
1280×720 guest texture. Joining exact texture versions to the same capture
found 1,274 bindings to the initial color destination `484626432`, 65 to
the tiled color destination `474877952`, and 15 to the depth destination
`497831936`. These are binding counts, not an estimate of visible pixels,
but the initial color version is a much broader dependency than the depth
version alone. Some bindings to the tiled color destination precede its
first tile write, so its retained starting version also matters. The next
implementation step is to produce the ordered
color and depth versions together, starting with the heavily reused initial
color resolve. Pinned compatibility versions remain a bring-up reference.
Color target aliases `0x00030000` and `0x000C0000` still share EDRAM base
zero and must not be split.

Evidence is in
`.local/ray-ui-native-promotion-20260925/ordered-copy-inputs-output/`.
Run `python tools/verify-ordered-frame.py ordered-frame-5001.csv
--require-owned-inputs` there to recheck the captured stream.
