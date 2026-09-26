# Ordered color tile replay — 2026-09-26

The selected-frame native path now owns a second 1280×720 color resource.
When the ordered stream reaches each main-scene color resolve, it copies the
completed region from the native scene surface into that resource. The
source and destination rectangles are validated against the captured copy
inputs and the three guest destination addresses must advance by exactly
1280×256×4 bytes. The path rejects missing, malformed, out-of-order or
incomplete tiles before presenting. The final native scene blit reads the
assembled resource; other frames still use the existing scene surface.

In the RelWithDebInfo continuous race run, output frame 5001 logged three
color copies with cumulative heights 256, 512 and 720 at their captured
ordinals. The route exited normally, the same-frame HUD replay succeeded,
and the selected frame was promoted. Its saved `native-shadow-5001.ppm`
remains a recognizable road/car/crowd/HUD image without a blank tile. The
strict ordered verifier passed: 3,425 owned draws, 94 copies, 20 clears,
417 pinned texture versions, and 88 complete nonzero-size copy inputs. The
UI verifier passed all 166 draws. Runtime evidence and images are under
`.local/ray-ui-native-promotion-20260925/ordered-color-tiles-output/`.

This is a bounded native replay of the **color tile copy operations**, not
a complete replacement for the game's resolve chain. The source is the
current approximate, single-sample UNORM native scene; the guest source is
4× MSAA 2:10:10:10 float. The initial full-size color texture at guest base
`484626432`, paired depth tile resolves, retained prior color versions and
all downstream reads still use compatibility-pinned resources or are absent
from native execution. Reconstruct those producers and route their exact
versions to consumers before removing duplicate guest work.
