# Sky writer in the moving race, 2026-09-27

A marked RenderDoc capture triggered by the race route's output-5008 image
contains the compatibility race and its three tiled main-scene passes. The
capture is ignored at `.local/ray-sky-lineage-capture-20260927/capture_capture.rdc`.
The saved `sky-lineage-trigger.ppm` confirms that clouds are visible in this
view. The third scene tile writes the four-sample 1280×512 float target
`ResourceId::2817`; its exported image at guest event 24767 contains the
same cloud pattern before the final composite.

RenderDoc pixel history for sample zero of that target gives the following
third-tile sequence. Values are linear float RGB after each event.

| Target pixel | After inherited copy 22117 | After draw 24655 | After draws 24679/24687 |
| --- | --- | --- | --- |
| `(600,100)` | `(0.260, 0.260, 0.260)` | `(0.206, 0.252, 0.251)` | `(0.208, 0.252, 0.251)` |
| `(600,150)` | `(0.260, 0.260, 0.260)` | `(0.183, 0.240, 0.253)` | `(0.185, 0.240, 0.253)` |
| `(100,100)` | `(0.260, 0.260, 0.260)` | `(0.228, 0.265, 0.250)` | `(0.241, 0.265, 0.248)` |

Draw 24655 is `VS=12BA4E86B158D049 / PS=CAE25D74AD7B16CB`, a
9,300-index draw with a 128×128 color texture and a 1280×208 viewport.
The selected ordered-frame census has that exact shader pair once per
main-scene tile, outside the current native families. Draw 24679 is the
already isolated `B4995BF113A7CE67 / 90CAB86BE8159DA8` 8,700-index
family that removed the D3D12 device on its third tile. Draw 24687 is
another 9,300-index family. Its contribution at the three sampled sky
pixels is negligible. These observations identify the first family as the
primary cloud/sky writer for this moving race view; they do not establish
that its original shader is safe to replay natively.

The earlier [sky lineage check](RAYMAN_SKY_PIXEL_LINEAGE_2026-09-27.md)
could not anchor the scene target to a matching race view. This capture
provides that anchor and rules out a final-composite-only sky explanation.
It also explains why a float target plus quarter-scale conversion of the
current native draws left the sky flat: the principal guest draw was absent.

Next isolate **one draw of `12BA4E86B158D049 / CAE25D74AD7B16CB`** in
the third tile with its exact indexed geometry, vertex transform, texture
version, depth and raster state. First verify GPU safety and the sampled
sky pixels in shadow; only then extend the safe behavior across all three
tiles and rolling frames. Keep whole-frame fallback and the current live
renderer untouched until several moving final images improve. The prior
8,700-index third-tile `DEVICE_HUNG` is a reason to test this draw alone,
not evidence that this distinct family also hangs.
