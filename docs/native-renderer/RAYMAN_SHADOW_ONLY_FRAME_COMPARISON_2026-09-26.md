# Shadow-only native/guest frame comparison

The first apparent near-match was invalid. In a live native run, the image
read from `guest_output` at output frame 5004 was byte-identical to the native
shadow saved at frame 5001. A previously promoted image can survive in a
reused output buffer, so a later read of that buffer is not an independent
compatibility reference.

The comparison now saves a bounded five-frame guest-output window only when
the native replay is in shadow mode (`pinyon_shift_native_race=false`). It
saves the native shadows in the same run and never replaces the guest image.
The selected route used `manager-rejection-short.fh1test` extended to stop at
5009, with capture and shadow starting at 4992 and the ordered trace at 5001.
The run exited normally. Local images are under
`.local/ray-ui-native-promotion-20260925/shadow-only-pair-output/`.

Native shadow 5001 shows race time 00:29.311 and guest reference 5002 shows
00:29.303. The car, camera and signs are close enough to assess large visual
differences. Across the 1280×720 RGB images, only 0.57% of pixels match
exactly and mean absolute channel difference is 54.13/255. These numbers
are descriptive, not a quality gate; a frame-time offset and animation still
contribute to the difference.

The largest visible gaps are a flat blue sky instead of the textured sky,
missing building and event signage detail, pale road and trackside ground
instead of lit asphalt, absent car paint/window detail and shadow, and a
degraded navigation graphic. The HUD remains readable, and the road, car,
barriers, arrows and crowd remain recognizable. This is a usable diagnostic
shadow, not yet the intended complete native presentation quality.

The selected ordered capture passed `verify-ordered-frame.py
--require-owned-inputs` and `verify-ordered-ui-capture.py`. It contains 4,398
draws, 94 copies, 20 clears and a complete 166-draw HUD suffix. Of the draws,
2,367 have native support and 2,031 remain unsupported in this run.

Next, prioritize the full-scene color/depth history and its consumers over
another isolated large indexed family. Identify how the later tiled color
resolves and lighting/postprocessing form the lit sky, buildings and asphalt,
then replay or preserve their ordered inputs. Keep the third-tile indexed
family excluded until its `DEVICE_HUNG` cause is measured. Repeat this
shadow-only comparison after a visible layer changes.

A bounded trial sampled the pinned first `484626432` color version as a
fullscreen starting image after the main clear, then ran the existing native
draws. It exited normally, but the native shadow had a nearly white sky and
washed-out HUD; the near-aligned guest comparison had mean absolute error
71.29/255. The source is not a suitable final-scene backdrop. That trial was
removed and the safe binary rebuilt. Trace the later tiled color resolves and
lighting/postprocessing path instead of directly blitting this early version.

In this same selected stream, the last main color-tile resolve is at ordinal
`11822064`. Before the first HUD draw at `11822217`, 126 draws remain outside
the native families. Two final draws at `11822215` and `11822216` use the same
original vertex/pixel pair and five pinned textures each, with 1280×360
upper/lower viewports. Earlier in that interval, two 640×360 single-texture
draws and many small resolves build intermediate inputs. This is a bounded
post-scene composite candidate.

A shadow-only trial of that pair identified the necessary bindings but did not
produce an owned native scene. The original Xenos primitive is a rectangle
list, so drawing its three vertices as a D3D triangle produced diagonal
wedges. Expanding the fourth corner filled the frame, but the image was a
flat olive gradient. The installed shader manifest explains this: the pixel
shader `614588022744BF6B` assigns descriptor slots to fetches 5, 0, 2 and 7
in that order, while the capture lists fetches 0, 2, 5, 7 and 17. The vertex
shader `20A41D46F34D238E` uses fetch 17 at slots that overlap the pixel
stage's slots, so the two stages need separate descriptor constant buffers.
Correcting those bindings restored a detailed scene, but it appeared rotated
180 degrees while the separately replayed HUD stayed upright. The 1280×720
scene input was the pinned guest texture at fetch 0, allocation 79 in that
run. Its detail therefore does not demonstrate native scene ownership.
The run exited normally in shadow mode. Evidence is in
`.local/ray-ui-native-promotion-20260925/composite-descriptor-corrected/`.
The trial was removed and the safe native binary rebuilt.

The [input-chain follow-up](RAYMAN_COMPOSITE_INPUT_CHAIN_2026-09-26.md)
identified fetch 0's three tiled color resolves and substituted their owned
native target in shadow. An identity blit corrected the 180-degree rotation,
but color remained visibly worse than the existing native shadow. The
composite still read guest-produced 320×192 and 640×360 intermediates. The
trial was removed; replay and compare those upstream versions before retrying
the final pair.
