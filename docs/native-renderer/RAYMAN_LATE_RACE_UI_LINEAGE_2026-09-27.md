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

The frame-5200 ordered copy stream narrows the first UI draw's input lineage.
Its 1280×720 fetch uses guest base `497831936`, allocation 43, generation
3599; the 128×128 fetch uses base `366587904`, allocation 1248, generation 1.
The larger allocation was already sampled at generation 3598 before the
late copy. A 256×256 copy to that same base at ordinal 12623261 is followed
by the minimap draw at 12623264, where the pinned generation is 3599. The
earlier full-size copy at 12621097 and 1280×256 copy at 12621866 also target
this base. The final small copy is therefore on the direct producer-to-fetch
path, not an unrelated UI state change.

The independent guest RenderDoc capture of race output frame 5030 has the
same first UI shader pair, 48 indices and 1280×720 plus 128×128 inputs. At
event 24414 those inputs are RGBA8 typeless (`ResourceId::4967`) and BC4
UNORM (`ResourceId::17094`). The RGBA texture already contains the bright
green navigation route in its upper-left 256×256 region: color
`(66,236,87,255)` occurs 999 times in the raw readback. The ignored
diagnostic exports are under `.local/native-renderer/ui-sameframe-20260925/`
as `first-ui-inputs.json` and `first-ui-input-0.png`. This older capture
establishes that the original producer supplies the green route *before*
the UI shader. It does not prove the frame-5200 native snapshot contains the
same pixels.

A new bounded RenderDoc capture of guest output frame 5200 confirms that
finding on the selected race route. At first UI event 24900, the 1280×720
RGBA input (`ResourceId::5043`) has 1,607 bright-green pixels in its
upper-left 256×256 region; its raw SHA-256 is
`66F85F6403F0D852A8AE27D2EC374FCF60ED9FAD41CC726B51AFE527E8BE804A`.
The second input is the same 128×128 BC4 payload seen at frame 5030. The
capture is ignored at `.local/ray-minimap-readback-20260927/` (RDC SHA-256
`FE7D54F2BFB69AB7FF352AF1469AA880B706B7E3D4303CDC2ADFEF919E33ABD6`).
The RenderDoc-wrapped run exited normally but did not execute native shadow
replay, so it provides no native-bound input comparison. Its guest image
cannot establish whether allocation 43/generation 3599 is green when native
UI replay samples it in a normal run.

Next, read back allocation 43/generation 3599 at the selected frame's guest
draw and at native binding, including its upper-left 256×256 region. If the
green pixels are missing, trace the 12623261 copy through immutable snapshot
publication and repair that producer/version path. If the pixels match,
inspect the original shader's descriptor and constant bindings, then compare
the first draw's output before later UI draws. Keep whole-frame fallback and
the original pre-UI handoff available while testing. Do not repeat blend or
sampler guesses without new input evidence.
