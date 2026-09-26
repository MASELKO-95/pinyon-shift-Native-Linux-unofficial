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
| Main tile 1 | depth (4), base 1024 | `497831936` | 1280×256 |
| Main tile 2 | depth (4), base 1024 | `499142656` | 1280×256 |
| Main tile 3 | depth (4), base 1024 | `500453376` | 1280×208 |
| Final presentation | color (0), base 0 | `471109632` | 1280×720 |

The three tile destinations are consecutive sections of one 1280×720
guest depth texture. Earlier pinned versions of that texture are sampled
by offscreen and main-scene draws. The next implementation step is to make
the native depth target and its ordered resolves produce the versions those
draws consume; pinned compatibility versions remain a bring-up reference.
Color target aliases `0x00030000` and `0x000C0000` still share EDRAM base
zero and must not be split.

Evidence is in
`.local/ray-ui-native-promotion-20260925/ordered-copy-inputs-output/`.
Run `python tools/verify-ordered-frame.py ordered-frame-5001.csv
--require-owned-inputs` there to recheck the captured stream.
