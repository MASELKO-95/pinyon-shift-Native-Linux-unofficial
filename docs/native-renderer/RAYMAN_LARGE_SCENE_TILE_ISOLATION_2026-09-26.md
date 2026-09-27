# Large indexed scene draw: tile isolation — 2026-09-26

The unsupported main-color family `VS=13013533689238769255`,
`PS=10433354260351917480` has one 8,700-index triangle-list draw in
each of the 256/256/208-height tiles. A bounded replay decoded the
captured big-endian 32-bit indices, verified them against the 1,501-vertex
input, and used the original shaders, textures and selected-frame order.
Every run below reached source frame 5001 and logged the extra scene draw;
none of the trial code was retained.

| Replay of this family | Short route result |
| --- | --- |
| First triangle of tile 1 | Normal exit |
| First 100 triangles of tile 1 | Normal exit |
| Full tile 1 only | Normal exit |
| Full tiles 1 and 2 | Normal exit |
| Full tile 3 only | `DEVICE_HUNG` after submission |
| Full tile 3, original transform, 16-pixel scissor | `DEVICE_HUNG` |
| Full tile 3, first-tile D3D12 viewport/scissor but original system constants | `DEVICE_HUNG` |
| Full tile 3, first-tile Y NDC scale/offset and full-height D3D12 viewport, bottom-tile scissor | Normal exit |
| All three, with only tile 3 normalized as above | `DEVICE_HUNG` |

The selected-frame state artifact shows the same active vertex/pixel shader
constant registers across tiles. The important varying system words are
`ndc_scale.y` and `ndc_offset.y`: tile 1 uses approximately `1.0` and
`-0.00139`, while tile 3 uses `3.46154` and `2.45673`. This narrows the
standalone tile-3 failure to its viewport conversion or its interaction
with the translated shader. The all-three failure after tile-3 normalization
also leaves aggregate GPU cost or an additional interaction unresolved.
The 16-pixel scissor failure argues against a simple pixel-count threshold.
The D3D12 log reported present failure `0x887A0005`, removal reason
`0x887A0006`; the crash reports did not identify a CPU fault. These trials
do not prove a correct replacement transform, and the saved safe-run images
showed no obvious improvement to the missing background.

The first-tile, two-tile and normalized-third runs saved native shadows under
`.local/ray-ui-native-promotion-20260925/ordered-large-first-tile-full-output/`,
`ordered-large-two-tile-probe-output/` and
`ordered-large-third-normalized-bottom-output/`. The failed all-three runs
are under `ordered-large-three-tile-probe-output/` and
`ordered-large-three-tile-normalized-output/`. The safe renderer source and
RelWithDebInfo binary were restored after the trials.

Next: inspect the translated vertex position path and measure GPU duration
around this family's tile draws in the existing D3D12 replay. Any proposed
normalization must preserve the guest's full-frame mapping and survive all
three tiles plus a longer drive. Keep the family out of live admission until
then. The smaller 220-vertex textured strip also ran in one tile without an
obvious visible improvement; it was removed rather than added speculatively.
