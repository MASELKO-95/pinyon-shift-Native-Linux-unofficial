# Ordered race-frame seam: first bounded capture

RAY-00 evidence, 2026-09-25. This is a capture-seam decision, not a complete
replayable frame. Local files: `.local/native-frame-seam-20260925/` and the
AppData preview log session `20260926T011548Z-p38080`. The RelWithDebInfo
preview exited normally after the `fh1-native-ui-admission-stress.fh1test`
route. It used the installed preview save without modifying it.

The run kept compatibility output visible while native scene capture was
enabled from source frame 6500. The bounded SNR-01 trace started at 6801 with
the following-frame flag. A new copy observation field carries the same
`IssueDraw` ordinal as prepared draws, so draws and resolves can be sorted
together. No production output behavior changed.

| Output image / source frame | GPU draws | Copies | UI target draws | Indirect draws | Final 1280×720 resolve |
| --- | ---: | ---: | ---: | ---: | ---: |
| `scene-6800.ppm` / 6801, HUD visible | 2,535 | 95 | 65 | 2,535 | ordinal 8,998,524 |
| `scene-6802.ppm` / 6803, HUD absent | 2,586 | 92 | 0 | 2,586 | ordinal 9,004,086 |

The UI target is `surface_info=0x14000500`, `color_info[0]=0x000A0000`.
In source frame 6801 its 65 draws occupy contiguous ordinals 8,998,459–
8,998,523; the next ordinal is the final resolve to guest address
`471109632`. In source frame 6803, there is no format-10 draw on **any**
target, yet the final resolve still writes that address. Both images show the
same race view; the first has minimap and speedometer and the second has no
HUD. This independently reproduces the earlier missing-pass finding. A
replayer cannot synthesize a HUD when the guest emits no UI pass.
The visible frame's UI uses only three shader pairs (42, 21 and 2 draws),
giving the first offline UI replay a bounded shader target.

The selected seam is **consumed GPU work**, where indirect/deferred commands
have already resolved into draw state. The title-level wrapper census found
that its 132,568 exact matches were copies; in this new pair, every logged
prepared draw has an indirect execution ID. Extending title wrapper discovery
would miss the bulk of this race frame. The existing prepared-draw and copy
observers are the starting feed; original shader pack, output hook and current
resource-version work remain reusable.

Coverage is not yet sufficient for offline replay. Within each sampled source
frame the combined draw/copy ordinals have no duplicates, but 48 `IssueDraw`
ordinals have neither a prepared-draw nor copy observation. These may be
nonrendering early exits: `IssueDraw` increments the ordinal before the
`fh1_mip_replacement_active_` fast return, which is the only top-level return
that bypasses both observers and the common return path. A per-frame count
has not yet verified that all 48 gaps take this branch. The prepared
observer does not provide immutable vertex/index inputs for every draw, and
final draw state and texture identities are currently sampled only for known
families. UI payload, clear-as-draw state, and resolve dependencies therefore
remain open. The separate copy and draw callbacks also need one compact
ordered frame record for replay rather than log reconstruction.

Next: classify the 48 gaps and extend the same consumed-command seam to
capture immutable inputs and final state for one HUD-visible frame. Then
replay that frame offline. Keep the missing-HUD producer/scheduling issue
visible in live-shadow validation; an output compositor is not its fix.
