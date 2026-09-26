# Live character-manager replay, 2026-09-26

The live race now includes backend-owned `SNR03M1` manager draws. ReXGlue
commit `fc39ae9` retains one stable vertex/index snapshot per observed range
per frame. Host commit `57eff20` joins prepared draws to their final state,
parses the owned manager fixture in the live path, and issues its geometry
with the recorded viewport, scissor and depth state. The pixel material is
still an opaque gray approximation.

Validation used the existing AppData preview state and the short saved race
route. The final route used `tools/launch-preview.ps1 -Configuration
RelWithDebInfo -StateRoot <installed .local/preview> -RenderTestScript
.local/ray-ui-native-promotion-20260925/manager-rejection-short.fh1test
-RenderTestOutput
.local/ray-ui-native-promotion-20260925/manager-final-state-output
-RenderTestTimeoutSeconds 900 -Hidden` with native race/UI and info logging.
The route exited normally. At source frame 5000 the log reports 30 manager
packets, 90 manager draws, `rejected=false`, and native scene admission with
`manager=true`. The captured `manager-frame.ppm` visibly includes spectators.

The rebuilt `pinyon_shift_snr04_owned_scene_diagnostic` replayed the earlier
102-draw fixture with 33,307 covered pixels, matching its prior baseline.
The RelWithDebInfo preview build and `git diff --check` passed. These results
show the crowd geometry on this scripted route; they do not prove matched
color, cross-family source order, unscripted stability or improved frame time.
