# Late-race UI lineage, 2026-09-27

The AppData-backed race route saved final native shadows and unpromoted
compatibility references from the **same run** at output frames 5200–5203.
The output log reported `scene=true ui=true promoted=false` for 5200–5203,
so the guest references were not contaminated by an earlier native copy.
The frame-5202 pair is under ignored
`.local/ray-late-final-pair-20260927/pair-5202.png`. Native race time and
car position are close to the guest reference. The HUD text is readable, but
the native navigation graphic is dark where the guest has a bright green
route. The native car rear, ground lighting and upper background also remain
visibly rough.

This run's selected frame 5200 has 2,981 ordered events: 2,867 draws,
94 copies and 20 clears. All draws have captured geometry and final state;
411 exact texture versions are pinned. Its 166 complete UI draws are a
contiguous final suffix. The first UI draw (ordinal 12623264) uses the
original `984DBF6AF14DBEBD`/`6FDA0F1CDE67D12F` shader pair, 48 indices,
and 1280×720 plus 128×128 texture fetches. The earlier same-frame RenderDoc
boundary study shows this shader/count starts the minimap. The existing
ordered-frame and UI verifiers passed on these artifacts.

A temporary selected-frame scene-only shadow probe saved output 5200–5203
without UI replay. The navigation graphic disappeared entirely, while the
world and car remained. This localizes the dark graphic to UI replay or its
inputs rather than the scene draw. The probe was removed after the run.

The existing opt-in pre-UI handoff was then run on the same race route. At
output 5200 it drew the owned native scene before the game's original HUD;
the saved frame has the bright green navigation graphic and readable HUD.
At output 5100, the late replay path was still used and its navigation was
dark. Late replay also appeared at 5202 and 5204. Thus the handoff improves
producer frames but would alternate UI appearance until its fallback is fixed.
This is evidence for using the original HUD, not yet grounds to make the
handoff the default.

The first ten guest UI draws in a selected-frame trace all used raw blend
control `0x07060706`, or source-alpha/inverse-source-alpha for color and
alpha. Native replay already matched the color factors. Changing its alpha
source factor to source-alpha did not improve the final navigation graphic.
A separate wrap-sampler trial also left it dark and affected gauge artwork.
Both trials and their temporary traces were removed; the normal binary was
rebuilt. Trial pairs are under ignored `.local/ray-ui-alpha-trial-20260927/`
and `.local/ray-ui-wrap-trial-20260927/`.

Next, trace the first UI draw's two **actual pinned texture contents and
generation** at the time of guest execution and native replay, then compare
its output before later UI draws. If those inputs differ, move the required
ordered producer/resolve into the rolling live path. If they match, inspect
the original shader's descriptor and constant bindings. Keep whole-frame
fallback and the original pre-UI handoff available while testing. Do not
repeat blend or sampler guesses without new input evidence.
