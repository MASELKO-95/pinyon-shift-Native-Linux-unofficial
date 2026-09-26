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

The cache's `last_update_accumulated_render_targets()[0]` is the live depth
target during the color/stencil clear. `GetFullyOwnedRenderTarget` is not a
safe late lookup: it requires every EDRAM ownership range to match one key,
and subsequent draws can change ownership. The smallest safe handoff is to
snapshot that depth resource immediately after the qualified color/stencil
clear, retain the copy through the native submission, and expose it to the
native output context. Native replay can bind its copied DSV for the eleven
color draws and restore the borrowed resource state afterward. The snapshot
and DSV must use the source resource's actual dimensions, format and sample
count; reject the replay if any differ from the expected shape. Do not bind
the native color version to visible consumers until its diagnostic preview
resembles the pinned guest version.

This is an implementation direction, not a completed depth handoff. The
current compatibility texture remains the visible consumer input. Source
evidence: `ordered-frame-5001.csv` and `ordered-frame-6782.csv` under the
local `ray-ui-native-promotion-20260925` capture directory, plus
`native-shadow-5001.png` and the guest producer preview.
