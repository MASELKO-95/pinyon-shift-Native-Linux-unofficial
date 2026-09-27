# Owned first post-scene downsample

The selected source-5001 ordered replay now draws the first 320×192
post-scene pass into an owned D3D12 target. Its original vertex shader is
`2C53E1A563484076`; its original pixel shader is `E17BECBE8BE65806`.
The draw contains eight Xenos rectangle-list primitives and samples fetch 0
at guest base `474877952`, the three-tile main color target already owned as
native `color_tiles`. A geometry shader supplies each rectangle's fourth
corner. The one-texture descriptor uses the translated pixel shader's
captured binding, while its SRV points to native `color_tiles`.

The target is 320×192. At the captured 320×192 resolve to guest base
`480858112`, the native target transitions from render-target to shader-
resource state. This preserves the first version needed by the following
64×32 reduction. The pass runs only for the explicitly selected ordered
trace frame; ordinary native race frames retain their existing path.

An opt-in `--pinyon_shift_native_downsample_probe=true` shows that target
in the lower-right of the shadow capture. The probe only runs with native
presentation off. The route
`manager-rejection-short.fh1test` exited normally and saved
`.local/ray-ui-native-promotion-20260925/downsample-owned-probe-3/native-shadow-5001.png`.
The eight rectangles produce a coherent small race image without the
diagonal gaps seen when a rectangle list is treated as triangles. The
image is bright and later HUD draws cover part of it, so this probe does
not establish color fidelity against the guest intermediate.

The same route with the probe off exited normally and saved a full native
scene with readable HUD under `downsample-normal-shadow/`. No native output
was promoted in either run. Build validation used
`cmake --build out/build/win-amd64-relwithdebinfo --config RelWithDebInfo
--target pinyon_shift --parallel 8`.

Next, replay and version the small reduction/feedback chain that follows
this copy, then compare the resulting 320×192 texture with the pinned guest
version used by the final composite. The 640×360 depth/color-fed pair remains
separate work. This first owned downsample is not yet connected to the final
composite or live presentation.
