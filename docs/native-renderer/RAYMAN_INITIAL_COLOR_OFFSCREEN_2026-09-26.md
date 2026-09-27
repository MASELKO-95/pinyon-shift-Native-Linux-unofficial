# Initial color offscreen producer — 2026-09-26

The selected-frame D3D12 replay now allocates a separate 1280×720
`R8G8B8A8_UNORM` target for the initial color texture at guest base
`484626432`. At the captured post-clear ordinals it issues eleven indexed
triangle-strip draws with their original vertex and pixel bytecode, owned
geometry, constants and pinned textures. It copies the target into a first
owned version, runs the four-vertex feedback draw, then makes the second copy.
Both versions and the target live
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
identifies 498 depth-writing draws before the eleven color draws. The
selected-frame pilot snapshots their 1× depth at its resolve and binds the
copied DSV. This reveals some scene structure, but the native version still
lacks earlier color history. A full-resource snapshot before the first 1×
color draw was nearly white: EDRAM ownership and tile materialization make
that cache resource an unsuitable initial image. Copying the pinned guest
first version directly into the native RGBA target also failed its D3D12
format/shape check.

The bounded feedback pilot instead seeds the native RGBA target through a
fullscreen texture read from that pinned first version after the first native
copy, then executes the original feedback draw with its original depth state
and pinned texture. The diagnostic second-version image at
`ordered-feedback-seeded-depth-preview-output/native-shadow-5001.png`
contains the crowd, scenery, vegetation and car without the previous white
wedge. The short route exited normally. This is a hybrid second version:
the first native copy remains incomplete and the seed depends on the guest
resolve. Visible consumers still use pinned compatibility textures. Compare
the second native version with its same-frame guest version and route it to
one consumer only when the result is safe under the whole-frame fallback.

The final normal-output rerun is
`ordered-feedback-seeded-final-output/`: it exited normally, saved four
consecutive native shadows, and logged twelve offscreen draws, three scene
draws and both initial color copies at source frame 5001. The strict ordered
frame verifier passed with 2,846 owned draw inputs, 97 copies and 21 clears;
the retained source-5000 HUD capture passed all 166 draws. The visible frame
still has prominent yellow background artifacts, so this pilot does not
complete the broader scene replay or visual-gap backlog.
