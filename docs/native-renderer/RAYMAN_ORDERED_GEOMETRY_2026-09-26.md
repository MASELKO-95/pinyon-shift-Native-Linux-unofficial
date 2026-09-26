# Owned geometry for one ordered race frame, 2026-09-26

The selected-frame RAY-00 capture now owns the vertex and index bytes of
every prepared draw. The SDK's opt-in trace path snapshots previously
uncovered prepass and intermediate draws with a 4 MiB per-range and 512 MiB
per-frame bound. Existing native-family and UI snapshots remain in use. The
host stores distinct byte strings once in `RAYGEO01` and records their
content-hash and length beside each ordered draw. This path runs only for
`--pinyon_shift_snr01_trace_source_frame=N`; live native presentation does
not pay for the broad snapshot.

A baseline source-5000 census found 1,964 of 2,937 draws with all geometry
snapshots. The 866 missing index snapshots and 1,312 missing vertex fetches
were concentrated in prepass/intermediate targets; both main race color
targets already had complete geometry snapshots. The missing vertex ranges
covered about 9.5 MiB by distinct guest address and length. An expanded
capture admitted all 2,311 draws in its own source-5000 run. Draw counts
vary between runs, so coverage is assessed within each capture.

The RelWithDebInfo build and saved AppData race route exited normally with
native race, live UI and the selected trace enabled. The final artifact is
under `.local/ray-ui-native-promotion-20260925/ordered-geometry-owned-output`.
Its source frame 5000 has 3,214 ordered events: 3,100 draws, 94 copies and
20 clears. All 3,100 draws have final state, geometry snapshots and verified
blob references. The `ordered-geometry-5000.bin` artifact has 1,235
content-hashed blobs totaling 13,962,688 payload bytes. Thirteen small
unreferenced blobs came from prepared draws later classified as clears; no
draw reference is missing. The frame also has 166 complete UI draws and
5,688 valid texture-version keys, no failed copies and no outdated textures.
The 48 ordinal gaps remain the previously classified mip skips. Output
frame 5001 promoted this source frame with its own UI pass.

The same stream has 5,688 texture bindings but only 448 distinct exact
fetch/version keys (394 allocation/generation pairs). The existing native
material snapshot pins selected D3D12 resources for live replay; it does
not export their pixels. The next capture step should deduplicate by these
exact keys and read back only the selected frame's required versions.

`verify-ordered-frame.py` checked the ordering, state, every referenced
geometry byte/hash and the UI artifact. `verify-ordered-ui-capture.py`
verified the separate UI fixture. RAY-00/01 still need immutable texture
pixels, intermediate target versions, and offline execution of the ordered
draw/clear/resolve stream; owning geometry alone does not establish a full
frame replay.
