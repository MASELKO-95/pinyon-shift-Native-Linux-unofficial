# Live final-frame baseline

The `continuous-race.fh1test` route exited normally with the installed
AppData preview save. Native presentation was off, UI shadow replay was on,
shadow capture began at output 5032, and the selected trace frame was 5040.
The run saved native shadows and uncontaminated guest references for output
frames 5040–5044 under `.local/ray-live-first-baseline-20260926/`.
Each of those five frames logged `scene=true ui=true promoted=false`.
The ordered replay marker appeared at 5040, and the strict selected-frame
verifier can inspect its captured stream. A contact sheet of frames 5040
and 5044 is `final-pairs-5040-5044.png` in the same directory.

The HUD text and speedometer are readable and the car remains recognizable.
The largest playability gap is the road and adjacent ground: native output
has broad flat green/gray regions where the guest shows textured dirt,
asphalt and shadow. The navigation graphic is degraded, and car lighting
and background structures also differ. Across the five same-run final-frame
pairs, whole-image RGB MAE is 44.98–48.21/255. Their source/UI timing is
near-aligned rather than pixel-synchronized, so the metric is for tracking
large visual changes, not a byte-match gate.

Start RAY-04 with road/ground readability. Identify the ordered draw or
target-version dependency behind the broad flat region, then land one safe
producer slice in the continuously presented native path. Compare several
moving final frames and repeat the HUD/fallback route before accepting it.
Keep the known large indexed `DEVICE_HUNG` families quarantined unless this
region's lineage specifically requires one.

Follow-up triage: the 30-draw `1193B16753866698`/`93961AB9BDF347DD`
road family already samples its asphalt texture in the continuous path. The
flat green area is therefore not evidence that this material needs another
shader trial. The selected-frame ordered path builds two versions of the
earlier color resolve at guest base `484626432`, but the continuous path
skips that producer and its copies. The capture records many later readers
of this resolve. Recheck the flat region after carrying that producer across
moving frames before expanding any other road material.

A trace-off repeat (`.local/ray-live-trace-off-20260926/`) exited normally,
but the two runs diverged visibly before the comparison frame (their guest
`live-5040.ppm` images differ by 18.25/255 mean RGB). It cannot isolate the
producer's effect. In the original same-run pair, the ground-region error at
the selected ordered frame 5040 is close to the adjacent continuous frames.
The earlier resolve is thus a concrete missing dependency, not yet a proven
cause of the flat ground. Require a same-run on/off probe or live final-frame
gain before expanding its executor.
