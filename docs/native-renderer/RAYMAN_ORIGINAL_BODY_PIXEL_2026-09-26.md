# Original car-body pixel program, 2026-09-26

The native remainder path now loads the original eight-texture body pixel
program `E9CD565D9C61D037/0x16003F` for its exact captured vertex/pixel
pair. It binds the captured packed pixel constants, bool/loop word, and
source-frame snapshots for fetches 1, 0, 13, 2, 3, 4, 5 and 6. The first six
textures are exposed as single-slice 2D arrays and the last two as cubes, in
the shader manifest's descriptor order. Any missing or unpinned input rejects
the complete native frame.

The RelWithDebInfo preview built. The installed-save
`continuous-default-start.fh1test` route exited normally with native race and
live UI enabled; native scene and HUD were promoted through output frame
5031. The captured frame at
`.local/ray-ui-native-promotion-20260925/original-body-fixed-output/native-default-later.png`
shows specular body shading where the former placeholder was flat. The rear
body is darker, and the window remains opaque red. The silhouette and HUD
remain readable, but this is not a car-quality closure. The first trial
exposed a null-descriptor bug: the texture resources had moved into the
retained frame before the body views were created. The corrected run uses
those retained resources, and promoted native frames through 5031.

The same route also exited normally under Xenos. Its
`native-default-later` frame shows a dark red rear body, but the native and
Xenos route positions and game clocks diverge; these are not matched lighting
or simulation states. The native car reached 45 km/h in `race-moving`, then
stopped against a barrier in the later capture, so this script alone does not
establish sustained playability. The descriptor slots currently reuse each
resource's unsigned view for the signed slot and use linear filtering rather
than the manifest's anisotropic filter. Those choices, plus the surrounding
approximate scene lighting, need direct inspection before tuning the paint.

Next, arrange a matching game-state capture and inspect the shader's sampled
values before changing constants or adding a brightening approximation.
Verify an unscripted drive before calling the native race playable. The
larger visible gaps remain road/terrain, opaque scenery and alpha/depth
behavior.
