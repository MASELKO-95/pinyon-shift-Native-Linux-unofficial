# Initial color offscreen producer — 2026-09-26

The selected-frame D3D12 replay now allocates a separate 1280×720
`R8G8B8A8_UNORM` target for the initial color texture at guest base
`484626432`. At the captured post-clear ordinals it issues eleven indexed
triangle-strip draws with their original vertex and pixel bytecode, owned
geometry, constants and pinned textures. It copies the target into a first
owned version, runs the four-vertex feedback draw sampling that native
version, then makes the second copy. Both versions and the target live
through GPU submission. Other native world draws still use the normal
scene target.

This is a bounded producer pilot, not a replacement texture. Earlier target
history and depth behavior are still missing; the pilot disables depth
testing and does not issue the preceding depth-only work. A diagnostic
blit of the second native version showed broad green/yellow regions, while
a blit of the pinned guest first version showed the complete race scene.
Changing the native target from `R10G10B10A2_UNORM` to the guest texture's
`R8G8B8A8_UNORM` did not materially change the native image. The format
mismatch was real, but it does not explain the missing scene. Native
consumers therefore continue using pinned compatibility textures.

The RelWithDebInfo short route with native presentation enabled logged
`color_draws=11 feedback_draws=1 copies=2` at source frame 5001. The frame
promoted with its same-frame HUD, the visible race image remained readable,
and the route exited normally. The strict ordered verifier passed: 3,005
draws, 94 copies, 20 clears, complete geometry/state and 417 pinned
versions. The UI verifier passed all 166 draws. The normal-run artifacts
are under `.local/ray-ui-native-promotion-20260925/ordered-producer-final-output/`;
the native and guest diagnostic previews are under the sibling
`ordered-producer-rgba-preview-output/` and `guest-producer-preview-output/`
directories.

The [depth-prepass census](RAYMAN_OFFSCREEN_DEPTH_PREPASS_2026-09-26.md)
identifies the earliest missing input: 498 depth-writing draws precede the
eleven color draws, and the intervening clear preserves depth. Snapshot the
compatibility depth target at that clear, test the color pass against its
copied DSV, and compare previews before routing native versions to a visible
consumer.
