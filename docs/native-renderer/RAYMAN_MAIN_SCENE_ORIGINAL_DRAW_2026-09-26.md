# Main-scene original draw pilot — 2026-09-26

The selected-frame replay now issues one previously unsupported original
shader draw in each of the three main-scene tiles. This
`21FBB5F33759B350`/`CF453BD52292E8E8` pair is a four-vertex,
one-texture strip with a pinned guest texture, owned vertex bytes and
captured constants. The draw uses the existing ordered dispatch, native
color/depth targets, and the tile's recorded viewport and scissor. The
three sequences in the first census were 11101213, 11101999 and 11102547;
replay selects the same shader/target family in the live sampled frame.

The RelWithDebInfo short race completed normally with opt-in native output.
Source frame 5001 promoted with a readable same-frame HUD, and the native
image remained recognizable. The strict ordered verifier passed for 2,893
draws, 94 copies, 20 clears, complete geometry/state and 416 pinned texture
versions. The UI verifier passed all 166 draws. Artifacts are under
`.local/ray-ui-native-promotion-20260925/ordered-main-extra-output/`.

The saved image shows no clear visual improvement over the preceding route;
the moving scene and game time differ between runs, so it is not a matched
pixel comparison. The other 36 unsupported main-color draws in the initial
39-draw census remain unissued, along with the earlier offscreen/depth
history. This pilot proves the captured one-texture draw can be issued in
the visible ordered scene without losing race presentation. It does not
close the world quality or performance gap. Keep its guest texture pinned
until the native producer becomes coherent, then target a visibly missing
layer in the remaining main-scene families.
