# Initial color texture versions — 2026-09-26

The selected-frame native path now owns two 1280×720 color resources for
the guest texture at base `484626432`. It snapshots the current native
color surface at each of the two captured full-size copy ordinals. Both
copies must report a valid source, the expected EDRAM base `720`, format,
rectangle and destination shape. Missing or malformed copies reject the
selected-frame replay. The resources stay alive through GPU submission.

The captured producer sequence is more than two copies. It includes
depth-only draws, an offscreen clear, three indexed triangle-strip draws
that sample two textures each, another clear and eight more such draws
before the first full color copy. A four-vertex textured feedback draw
consumes that first version before the second copy. Those draws and their
target switches are not yet replayed natively. Therefore the two owned
resources contain native scene content as it stood at each ordinal, not
faithful guest texture pixels. Consumers continue to use their pinned
compatibility textures.
The next useful step is to replay this bounded offscreen producer and its
feedback draw into a separate native target, then bind the resulting
version to later native consumers.

A RelWithDebInfo continuous-race run copied both versions at source frame
5001, then logged all three ordered depth/color tile pairs. The selected
frame promoted with same-frame HUD, its saved image remained readable, and
the route exited normally. `verify-ordered-frame.py --require-owned-inputs`
passed: 2,877 draws, 94 copies, 20 clears, 413 pinned texture versions and
88 complete nonzero-size copy inputs. `verify-ordered-ui-capture.py` passed
all 166 UI draws. Runtime artifacts are under
`.local/ray-ui-native-promotion-20260925/ordered-initial-color-versions-output/`.

The later [offscreen producer pilot](RAYMAN_INITIAL_COLOR_OFFSCREEN_2026-09-26.md)
changed these snapshots to read from a separate native target. This note
records the earlier copy-only milestone.
