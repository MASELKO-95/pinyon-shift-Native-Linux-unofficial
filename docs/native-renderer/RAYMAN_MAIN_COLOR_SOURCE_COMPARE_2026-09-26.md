# Main color source versus final output

The shadow probe now exposes both 1280×720 inputs to the first 320×192
downsample: `--pinyon_shift_native_small_target_probe=7` shows its pinned
guest fetch-0 texture, and `=8` shows native `color_tiles`. Both keep native
presentation off and save the scene without HUD. The selected source-5001
and source-5002 routes exited normally. Their captures are under
`.local/ray-ui-native-promotion-20260925/{guest,native}-scene-source-probe/`
and the corresponding `-5002/` directories.

The guest source has a dark, complete road, car, signs and buildings. The
native source has much brighter flat sky, ground and structures, so the
first downsample receives different scene colors before its shader runs.
Near-aligned cross-run comparisons measured RGB MAE 78.03/255 at frame 5001
and 79.87/255 at frame 5002. These are visual triage measurements; the runs
are not pixel-synchronized.

A per-channel scale fitted to the two *source* frames reduced their MAE to
about 7–9/255, but it was the wrong final-output fix. The guest's later
postprocessing brightens its dark source substantially. In a full native
scene-plus-HUD shadow trial, applying that scale raised final-frame MAE
against the guest from 60.82 to 79.02/255 and made the race too dark. The
trial was removed and the original blit rebuilt. Do not select a display
grade from an intermediate-only score.

Next, compare the visible final frame and the main color/lighting producers
at the same game state. Improve or approximate the missing large-area
lighting and road/background work, then remeasure the final frame and driving
readability. The small reduction and feedback chain can wait until it has a
visible final-frame benefit.
