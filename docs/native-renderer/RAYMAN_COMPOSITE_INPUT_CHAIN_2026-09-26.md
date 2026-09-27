# Final composite input chain

The ordered source-5001 capture identifies the final composite's full-size
input. Fetch 0 has texture-fetch word 1 equal to `474878134`; its 4 KiB page
base is `(word1 >> 12) << 12 = 474877952`. Three color resolves write that
base and its 256-row and 512-row offsets, for a complete 1280×720 image.
The native replay already assembles those exact regions into `color_tiles`.
The capture is in
`.local/ray-ui-native-promotion-20260925/composite-owned-color-identity/`.

The other scene-dependent inputs are produced after the third main color
tile. Ordinals vary between runs; these are from that capture:

| Consumer input | Captured producer path |
| --- | --- |
| Fetch 0, 1280×720, base `474877952` | Three main color tile resolves, already represented by native `color_tiles` |
| Fetch 2, 320×192, base `501600256` | A 320×192 draw sampling fetch 0, followed by smaller reduction/feedback draws and two 320×192 resolves |
| Fetch 5, 640×360, base `480858112` | Two 640×360 draws at `11688848` and `11688850`, followed by two resolves; the first draw samples depth base `497831936`, the second samples the previous color at `480858112` |
| Fetch 7, 16³ LUT | Pinned 3D texture; no same-frame copy in this interval |
| Vertex fetch 17, 32×32 | Same-frame 32×32 resolve at `533356544` |

The two final 1280×360 rectangle-list draws sample these inputs before the
HUD suffix. Their shader descriptor order differs from the capture order:
pixel fetches 5, 0, 2, 7 and vertex fetch 17 require separate stage tables.
Their three-vertex rectangle needs fourth-corner expansion.

A shadow-only trial bound native `color_tiles` in place of fetch 0. The
preview route exited normally and produced a recognizable moving race, but
the image was rotated 180 degrees and had harsh green/pink/blue colors.
Using an identity presentation blit fixed orientation; color remained worse
than the current native shadow. The 320×192 and 640×360 inputs were still
guest-produced, and the trial did not isolate which missing native history or
format conversion caused the color error. Images are
`composite-owned-color-1/native-shadow-5001.png` and
`composite-owned-color-identity/native-shadow-5001.png` under the local
promotion directory. Both experiments were removed, and the safe binary
was rebuilt. Neither result is a candidate for live takeover.

Next, preserve the post-tile target/version sequence in the native ordered
replay. Start with the 320×192 downsample from owned `color_tiles`, then
replay the depth/color-fed 640×360 pair with exact versions and compare each
intermediate against its pinned guest counterpart. Retry the final composite
only when its scene-dependent inputs are owned. Keep it shadow-only until
both orientation and color are visibly coherent.
