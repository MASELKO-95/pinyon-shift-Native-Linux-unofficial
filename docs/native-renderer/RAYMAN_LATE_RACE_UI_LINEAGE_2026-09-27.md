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

An initial fixed-frame RenderDoc trial used the default Release executable,
which was still built on September 23. The native shadow implementation was
in the September 27 RelWithDebInfo build. That trial showed a green route in
another guest frame, but it did not capture output frame 5200 or native UI;
its purported frame-5200 alignment is withdrawn. The SDK's built-in
`PINYON_SHIFT_SNR04_RENDERDOC_TRIGGER_FILE` hook was then set to the render
test's `native-5200.ppm` marker, avoiding the SDK's automatic capture at
source frame 5000. The fresh RelWithDebInfo capture is ignored at
`.local/ray-minimap-readback-20260927/capture-inapp_capture.rdc` (SHA-256
`1A7CB435126C5AAAA36C2567259E46ABDF7BEBFDD8BDBDC632CC18255CE8A761`).
It contains both the original guest UI and the subsequent native shadow UI.

At the first minimap draws (guest event 35551, native event 68637), the
1280×720 RGBA inputs both contain the green route: 1,328 and 1,396 bright
green pixels, respectively, in their upper-left 256×256 regions. The
128×128 BC4 masks are byte-identical. The pixel float constants, post-VS
vertices and index streams also match exactly; texture view formats,
dimensions and channel swizzles match. The route is therefore lost *during*
the native first UI draw, not in its pinned source texture or a later HUD
draw. At output pixel (160,490), guest pixel history has one green primitive.
Native pixel history has that primitive in gray, followed by a second black
primitive covering it.

The captured guest pipeline culls back faces with counterclockwise fronts;
the native pilot culled none with clockwise fronts. The selected frame's 166
UI draws split cleanly by shader family: the 50 textured draws use raster
state `2195458`, and the 116 untextured draws use `2195456`. Native replay
now applies back-face culling with counterclockwise fronts to the textured
families while retaining no culling for the untextured family. In a new
same-run race shadow/reference pair, the lower-left navigation region has
1,728–1,735 green native pixels at output 5200–5202, versus zero before the
change and 1,716–1,727 in the corresponding guest references. The frame-5202
pair is ignored at `.local/ray-ui-cull-fix-20260927/pair-5202.png`; its HUD
and green route are readable. The game exited normally. This fixes the
navigation cue, while the rough car, ground and background remain separate
scene-rendering work.

The scripted hot toggle also exited normally: native-on, compatibility-off
and native-on-again frames all retained the green cue. The mode-boundary run
exited normally and retained the cue in race output and the compatibility
free-roam scene. Its `title-settled` image still has saturated background
noise with readable menu text. The same defect appears in pre-fix captures
at `.local/ray-manager-pointer-mode-20260927/title-settled.png` and
`.local/ray-manager-hash-mode-repeat-20260927/title-settled.png`; it is not a
regression from the UI raster change. The older
[scene ledger](SCENE_NATIVE_RENDERER_BACKLOG.md) already calls for a
compatibility-only control and better title checkpoint semantics before
title-transition signoff.
