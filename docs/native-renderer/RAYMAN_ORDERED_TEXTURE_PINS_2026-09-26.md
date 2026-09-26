# Pinned textures for one ordered race frame, 2026-09-26

The selected RAY-00 trace now pins every used D3D12 texture version for the
source frame, rather than only the materials used by the existing native
scene. It reuses the native texture snapshot path with a trace-only 512 MiB
budget and 1,024-version bound; normal live rendering keeps its existing
limits. A `ordered-texture-pins-N.csv` manifest records the exact six fetch
words, allocation and payload generation for each immutable GPU copy, plus
the resource description. These copies survive into the next output frame
for immediate replay; this is not a disk export of texture pixels.

The first broadened source-5000 run retained 270 of 271 distinct keys. The
only missing key was a 3D texture used by two intermediate-target draws.
The snapshot helper previously accepted only 2D and cube resources. Adding
the existing D3D12 whole-resource copy for 3D textures closed that gap: the
next run retained 417 of 417 keys with zero failed or limited attempts.

The final RelWithDebInfo route exited normally using the installed AppData
save and `.local/ray-ui-native-promotion-20260925/manager-rejection-short.fh1test`.
Its output is
`.local/ray-ui-native-promotion-20260925/ordered-texture-pin-manifest-output`.
The selected source-5000 stream has 3,004 events: 2,890 draws, 94 copies
and 20 clears. All 2,890 draws have owned geometry; all 415 distinct
texture keys used by 5,675 bindings exactly match the pinned manifest.
The GPU snapshots total 130,416,640 allocation bytes. The separate HUD
fixture has 166 complete draws. Runs vary in draw count, so coverage is
checked within each capture.

`verify-ordered-frame.py ordered-frame-5000.csv --require-owned-inputs`
passes; the same strict mode rejects the prior geometry-only capture
because it lacks the texture manifest. `verify-ordered-ui-capture.py`
passes on `ordered-ui-5000.bin`.

RAY-00/01 remain open. The pinned GPU copies are immutable inputs for
same-process replay, but intermediate render targets and ordered
draw/clear/resolve execution have not yet produced a full-frame offline
image. A portable replay after process exit would also need texture pixel
readback; it is not required for the first saved output image.
