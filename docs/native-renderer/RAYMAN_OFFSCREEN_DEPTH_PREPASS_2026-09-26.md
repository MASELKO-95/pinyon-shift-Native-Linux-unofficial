# Initial color producer depth dependency — 2026-09-26

The initial color producer is missing its depth input, not another color
format change. On source frames 5001 and 6782, a depth/stencil clear covers
guest rectangle `0:0:640:360` at depth base 0. Before the color clear, the
ordered stream then contains 522 draws on surface `0x14000500`, depth
`0x00010000`, color base 0. Of these, 498 have depth writes enabled; the
other 24 have depth disabled. They use 25 vertex shader hashes and seven
pixel shader hashes (including zero). A depth resolve to guest address
`497831936` occurs within this interval. The next clear targets color base
720 and stencil, **not depth**. Eleven subsequent indexed color draws use
depth testing with writes disabled and compare function `LESS_EQUAL`.

`RB_SURFACE_INFO=0x14000500` decodes to pitch 1280 and **1× MSAA**. The
earlier clear uses `0x0A020280` (pitch 640, 4× MSAA), but it does not make
the depth-writing pass a 4× target. `RB_DEPTH_INFO=0x00010000` selects
depth base 0 and `D24FS8`; the D3D12 cache creates a
`R32G8X24_TYPELESS` resource with a `D32_FLOAT_S8X24_UINT` view for it.
The native offscreen target is already 1× at 1280×720, so its color pass can
depth-test against a compatible 1× depth view. Its current pipeline
explicitly disables depth and binds no DSV, which explains the large
green/yellow polygons in the diagnostic preview.

The depth resolve to `497831936` is the precise snapshot seam. Its observed
source is a 1×, 1280×2048 `R32G8X24_TYPELESS` cache resource at depth base
0; the remaining 24 draws before the color clear have depth disabled. At
the later color/stencil clear, the cache changes to a 4× view, so a snapshot
there would be the wrong resource. `GetFullyOwnedRenderTarget` is also not a
safe late lookup: it requires every EDRAM ownership range to match one key.

The selected-frame pilot now copies the 1× depth resource immediately after
that resolve and retains it through native submission. Native replay checks
its shape, binds a read-only `D32_FLOAT_S8X24_UINT` view for the eleven
color draws, and restores the resource state. The RelWithDebInfo short route
exited normally with the snapshot, both offscreen copies and selected-frame
promotion. A diagnostic blit of the second native color version shows new
upper-scene structure compared with the depth-disabled pilot, but still has
a largely blank lower scene. The pinned guest version has extensive detail
there. A later full-resource snapshot before the first 1× color draw was
nearly white because the cache had not materialized the complete EDRAM tile
history. The bounded feedback pilot now seeds its second native version from
the pinned first guest version via a fullscreen shader; see the
[offscreen producer record](RAYMAN_INITIAL_COLOR_OFFSCREEN_2026-09-26.md).
Visible consumers remain compatibility-backed pending same-frame comparison.

Source evidence: `ordered-frame-5001.csv` and `ordered-frame-6782.csv` under
the local `ray-ui-native-promotion-20260925` directory. The normal-exit run
is `ordered-depth-snapshot-first-output/`; the diagnostic image is
`ordered-depth-preview-output/native-shadow-5001.png`, compared with
`ordered-producer-rgba-preview-output/` and `guest-producer-preview-output/`.
