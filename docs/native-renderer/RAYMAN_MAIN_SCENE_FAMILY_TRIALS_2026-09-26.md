# Main-scene family isolation — 2026-09-26

The retained-HUD selected frame has nine unsupported pixel-shader families
on the tiled main color target (`surface=335676672`, `color=786432`). Each
occurs once per 256/256/208-height tile. This is a small, bounded place to
expand original draw replay without introducing a new material renderer.

The first trial admitted only the 8,700-index triangle-list family
(`VS=13013533689238769255`, `PS=10433354260351917480`). Its captured
32-bit big-endian indices decode within the 1,501 captured vertices, and the
RelWithDebInfo build succeeded. On the UI-admission stress route, D3D12
removed the device with `DEVICE_HUNG` after ordered replay submitted six
scene producer draws. A repeat with `--d3d12_debug=true` reproduced the
hang. DRED reported breadcrumb 6710 of 8697 at operation type 4 (draw),
with no page-fault address or debug validation error. This identifies the
new family as unsafe in the current replay path; it does not prove whether
the fault is shader translation, a missing binding, or another GPU input.
The trial was removed before any commit.

The four-vertex, one-texture triangle strip
(`VS=2448750878616302416`, `PS=14935409525838244072`) was already the
admitted scene family. The support manifest's `-1` applies to the regular
scene snapshot, not to these selected producer draws. A duplicate trial
correctly failed the exact producer-count guard and was removed. Its first
run also lacked `--pinyon_shift_native_ui_shadow_start_frame=6774`, so it
only captured the frame. Neither run is GPU safety evidence for a new draw.

The next actual candidate was the indexed one-texture strip
(`VS=3799439062768750576`, `PS=9031833503928755479`). Its 16-bit
big-endian index range is 0–8,999 for 9,000 captured vertices. One frame
has a 14,816-index draw per tile; another splits it into 11,520 and 4,156
indices per tile. The first admission trial rejected the latter safely on
the exact-count guard. The revised admission bounds each draw at 16,384
indices, requires the captured vertex shape, and checks the decoded maximum
index. That trial issued nine scene draws at selected frame 6782, then D3D12
reported `DEVICE_HUNG` at frame 6785 and the process crashed. This trial was
also removed. Neither large indexed family is safe to admit yet.

Both failures happened after the extra indexed draws submitted, while the
existing small indexed offscreen producers and three non-indexed scene
strips remain stable. The shared cause is unproven. Investigate GPU inputs
and translated-shader behavior for one indexed draw using the D3D12 debug
layer and DRED before retrying either family. Keep the current whole-frame
fallback and do not add more large draw families on shape alone.

A later diagnostic replaced the one-texture strip's pixel program with the
renderer’s existing flat pixel program while keeping its translated vertex
shader and full captured index range. Nine scene draws issued, and D3D12
still reported device removal (`0x887A0006`) during presentation. This makes
the original pixel program alone an unlikely cause; vertex/raster work,
depth state, or draw cost remain possible. A 1,024-index-per-draw control
exited normally but did **not** open the shadow gate: that run had a different
65-draw UI state at frame 6782. It is not evidence that a shorter draw is
safe. Both diagnostics were removed from the renderer code.

On a later stress run with the expected race HUD, the flat-shader trial
issued the same nine scene draws with each indexed draw capped at 1,024
indices. The route again ended in D3D12 removal (`0x887A0006`). Full draw
count is therefore not necessary to trigger the hang. A three-index trial
with the debug layer enabled did not reach selected-frame replay: the
compatibility backend stopped advancing after frame 6060 and repeatedly
reported failed PM4 draws. Its result is inconclusive. The test route can
also land in free roam; a prior normal exit with 65 HUD draws never opened
the native shadow gate. Future GPU comparisons must verify the race HUD and
the `RAY01 original draw replay` marker in the same run.

The later [source-5001 tile isolation](RAYMAN_LARGE_SCENE_TILE_ISOLATION_2026-09-26.md)
reached that marker reliably. One triangle, 100 triangles, the complete first
tile, and the first two complete tiles passed. The complete third tile alone
reproduced device removal, including with a 16-pixel scissor. Its captured
Y NDC system constants are the key varying state; replacing them and using a
full-height host viewport let that tile finish alone, but all three still
removed the device. None of those diagnostic variants were retained.
