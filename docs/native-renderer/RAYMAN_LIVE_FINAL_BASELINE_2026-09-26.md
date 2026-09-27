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
