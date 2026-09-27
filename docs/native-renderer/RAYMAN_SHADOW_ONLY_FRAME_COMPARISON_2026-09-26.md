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
another isolated large indexed family. In particular, identify which initial
resolved color version carries the lit sky, buildings and asphalt into the
main scene, then replay or preserve its producer and ordered consumers. Keep
the third-tile indexed family excluded until its `DEVICE_HUNG` cause is
measured. Repeat this shadow-only comparison after a visible layer changes.
