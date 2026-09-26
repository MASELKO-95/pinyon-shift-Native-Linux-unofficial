# Original car-glass pixel program, 2026-09-26

A temporary pixel-hash color diagnostic identified the player's rear window
as `2E5E0A854BE00027`/`BDFFA72B7ED2FBA4`, specialization `0x16003F`.
The source-5000 remainder fixture contains 46 draws of this five-texture
pair. Its translated pixel program samples fetches 13, 1, 2, 3 and 4; fetches
2 and 3 are cubes. It reads bool/loop word 4 and dynamically indexes the
256-register pixel float bank, including registers 254 and 255.

The SDK now snapshots all five source-draw texture versions for this exact
pair. The R6 remainder record carries its bool word. Native output expands
the captured sparse pixel constants into the full register bank and binds
the two 2D-array views, two cube views and final 2D-array view in manifest
order. Missing or unpinned resources reject the complete native frame.

The RelWithDebInfo preview built. Both the shortened race-ready route and
the full `continuous-default-start.fh1test` route exited normally with native
race and live UI enabled. The full run promoted scene and HUD through output
frame 5031. At pixel (640, 450) in the race-ready capture, the window changed
from placeholder RGB `(166, 41, 31)` to the original shader's dark RGB
`(6, 30, 36)`. The later capture at
`.local/ray-ui-native-promotion-20260925/original-glass-long-output/native-default-later.png`
keeps the dark glass as the car and view change. This closes one conspicuous
flat window; it does not prove exact glass blending, reflection or lighting
parity. As with the body shader, signed descriptor slots currently reuse the
unsigned resource view and samplers use linear filtering.

The `fh1-native-race-mode-boundary.fh1test` route also exited normally on this
build. `verify-native-race-mode-boundary.py` found native race output and
complete compatibility output in the pause, free-roam and settled-title
captures.

The next visible targets are the dark rear paint, flat road/terrain and
opaque trackside scenery. An unscripted drive is still needed before calling
the opt-in native race playable.
