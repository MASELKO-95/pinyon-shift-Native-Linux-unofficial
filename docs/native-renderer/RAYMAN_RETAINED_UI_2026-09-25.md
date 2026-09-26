# Retained race HUD pilot — 2026-09-25

Some race frames issue no draws to the known 1280-wide HUD target while the
compatibility image still displays that HUD. The bounded native replay now
uses the immediately preceding complete captured HUD draw set only if the
current frame has no HUD draws, was not rejected, and contains no observed
draw, clear or copy event targeting that HUD surface. Older draw sets or
partial captures still fall back to the complete compatibility image.

This needs exact source textures for one extra output. In the first test,
source frame 5000 had no UI producer and reuse was attempted for output
5001, but texture allocation 43 generation 3398 was already unavailable;
promotion correctly stayed off. ReXGlue now keeps material snapshots through
the second following output instead of the first. The next hot-toggle route
replayed a no-producer source frame in shadow. Both RelWithDebInfo builds
and both routes exited normally.

The UI-admission stress route with native enabled, source window 6790–6813,
then showed a no-producer source frame 6791 and
`output_frame=6792 source_frame=6791 ui_frame=6790 scene=true ui=true
promoted=true`. The captured `scene-6792.ppm` shows the native world/car
with readable HUD; adjacent `scene-6794.ppm` updates race time and world,
and their hashes differ. A second stress run with the target-write guard
again promoted the no-producer 6791 frame. Evidence is under
`.local/ray-ui-native-promotion-20260925/retained-ui-*-output/`.

This is a one-frame retained-target inference, not a complete ordered target
version model. It does not cover consecutive no-producer frames, unobserved
target writes, longer drives, or UI draws from another target. The replay
window still ends after 24 source frames, and the native world needs visual
work before RAY-03 can be called playable.

Follow-up: the live window now records consumed draw, copy and clear events
that name this exact HUD surface. The guard checks those events before
reusing the previous draw set. A further UI-admission stress run exited
normally; source frame 6791 again had no HUD producer and output 6792
promoted the retained HUD. This narrows the inference to a frame with no
observed HUD-target writes. It still does not prove every possible guest
write or identify the final resolve/target version.
