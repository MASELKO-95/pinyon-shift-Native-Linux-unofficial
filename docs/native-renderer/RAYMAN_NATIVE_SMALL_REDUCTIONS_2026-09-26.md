# Owned 64×32 and 32×32 reductions

On selected source frame 5001, the ordered replay now follows the first
320×192 downsample with the original 64×32 and 32×32 rectangle-list draws.
The event ordinals below are from the pinned `downsample-owned-probe-3`
capture; fresh runs assign different ordinals.
The 64×32 pass uses VS `2C53E1A563484076`, PS `E17BECBE8BE65806`, and
the native 320×192 target resolved at event `11782905`. The 32×32 pass uses
the same VS, PS `AE59F518D522BDD1`, and the native 64×32 target resolved
at event `11782907`. Each owned target transitions to shader-resource state
at its captured copy event (`11782907` and `11782909`). The selected path
rejects an unexpected draw, source address, size, copy order, or format.

`--pinyon_shift_native_small_target_probe=2` displays the 64×32 target in
the bottom-right of the shadow output; value `3` displays the 32×32 target.
The probe suppresses the HUD in the saved shadow image and requires native
presentation to remain off. Both probe runs of
`manager-rejection-short.fh1test` exited normally. The 64×32 crop at
`.local/ray-ui-native-promotion-20260925/reduction-64-probe/native-reduction64-crop.png`
contains a recognizable, very bright reduced race image. The 32×32 crop at
`.local/ray-ui-native-promotion-20260925/reduction-32-probe/native-reduction32-crop.png`
is solid white. The pass is submitted and its target is sampled, but this
does not establish that its pixels match the guest intermediate. The ordinary
shadow route with these three reductions and the probe off exited normally
with a full scene and readable HUD.

The same shadow probe can display the pinned guest inputs consumed by the
next pass: `4` for 32×32, `5` for 64×32 and `6` for 320×192. The guest
32×32 input is nearly black, and the guest 64×32 input is a dark but
recognizable race image. The guest 320×192 image is also dark and coherent,
including the car and roadside signs. Crops are saved under
`.local/ray-ui-native-promotion-20260925/guest-reduction-{32-probe-2,64-probe,320-probe}/`.
All three guest-probe routes exited normally. This locates the visible
brightness divergence no later than the first native 320×192 pass; debugging
the white 32×32 result alone would miss the earlier mismatch. The source
native scene is already visibly rough and bright, so compare its main-color
tile against the guest tile before changing the downsample shader. That
[source comparison](RAYMAN_MAIN_COLOR_SOURCE_COMPARE_2026-09-26.md) confirms
a large upstream color difference and rules out a source-fitted display
scale as a final-frame fix.

The next draw (`11782910`, PS `ED74D20BC7DFB0F7`) combines the 32×32
target with another guest texture. The following 320×192 feedback draw
(`11782912`) reads guest base `501600256` before this frame's first copy
to that address (`11782913`), so its first input is retained history.
Improve the visible native scene first. If this effect chain is still needed
for the usable race target, compare its first input and 320×192 output at
matched game time, then provide an explicit history seed or persistent native
version for the feedback pass. Neither the remaining feedback chain nor the
final composite is owned yet.
