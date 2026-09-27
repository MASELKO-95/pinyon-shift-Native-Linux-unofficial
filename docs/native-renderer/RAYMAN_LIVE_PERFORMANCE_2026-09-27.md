# Live native race performance, 2026-09-27

The current renderer presents a continuous native race, but its frame time is
itself a playability blocker. The first controlled RelWithDebInfo CSV captures
used the installed AppData save, the same
`fh1-native-race-profile.fh1test` route and source-frame rows 5002–5029.
Compatibility was run without native capture flags. These are separate runs,
so the numbers rank CPU work rather than prove a synchronized visual A/B.

| Route | Median frame | p95 frame | Material snapshots/frame | Draw CPU | Native output CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| Compatibility | 23.54 ms | 25.94 ms | 0 | 0 | 0 |
| Native before reuse | 187.81 ms | 201.20 ms | 143 | 132.99 ms | 41.56 ms |
| Native with exact-version reuse | 118.34 ms | 128.59 ms | 6 | 61.30 ms | 41.23 ms |
| Native with CPU upload scratch reuse | 104.31 ms | 123.43 ms | 6 | 57.17 ms | 30.02 ms |
| Native without redundant manager rehash | 94.88 ms | 106.54 ms | 6 | 48.85 ms | 29.95 ms |

The original live path made a committed GPU texture and copy for every new
material key in every frame. The retained change reuses a previous frame's
immutable snapshot only when all six fetch words, allocation ID and payload
generation match. Changed versions still get a new copy, and selected-frame
trace inputs remain pinned. Material-snapshot CPU time fell from 53.89 ms to
3.65 ms median in the measured native window; total frame time fell 37%.

A simpler trial removed live pinning altogether and measured 95.32 ms, but
it was **rejected**: dynamic texture generations changed before output and
the scene fell back to compatibility. This is why the retained cache still
copies changed versions. Do not count that faster failed run as a native gain.

The retained run reported `scene=true ui=true promoted=true` for output
frames 5005–5029. A 300-frame scripted continuation exited normally;
presented captures 5040 and 5200 show the native scene and readable race HUD.
The mode-boundary verifier passed for race, pause, free roam and title, and
the hot-toggle verifier passed on/off/on. Artifacts are under ignored
`.local/ray-perf-native-20260927/`,
`.local/ray-perf-compat-clean-20260927/`,
`.local/ray-perf-native-reused-20260927/`,
`.local/ray-live-reused-20260927/`,
`.local/ray-mode-reused-20260927/` and
`.local/ray-toggle-reused-20260927/`.

At about 118 ms per frame, the renderer is still too slow for the requested
usable race. An opt-in `--perf_critical_path_trace=true` probe split the
roughly 41 ms native output callback: sampled frames 5010 and 5020 spent
about 28–39 ms in scene drawing and 3 ms in UI replay. Within scene drawing,
parsing took 5 ms, repeated draw preparation 17–19 ms, frame-resource
creation 5–14 ms, descriptor setup about 1 ms, and issuing prepared draws
under 0.3 ms of CPU time. These are two samples, not a population estimate.
The next bounded split found about 8 ms in manager bulk-data setup for
roughly 100 draws; their binding loop and texture lookup were under 0.2 ms
and 0.05 ms respectively. `UploadArena` had allocated and grown a fresh CPU
byte vector every frame before copying it into the D3D12 upload resource.
Reusing that CPU scratch per render thread reduced the measured output
callback from 41.23 to 30.02 ms and total frame time from 118.34 to
104.31 ms. The GPU upload resource is still created per frame. The extended
scripted route exited normally, with native promotion at captured frames
5040, 5100, 5200 and 5290; mode-boundary and hot-toggle verifiers passed.
Artifacts are under `.local/ray-arena-reuse-profile-20260927/`,
`.local/ray-arena-live-20260927/`, `.local/ray-arena-mode-20260927/` and
`.local/ray-arena-toggle-20260927/`.

The manager observer also rehashed every previously copied geometry range on
each draw, after the first copy had passed its hash check and the repeated
copy had passed an exact byte comparison. Removing that redundant hash lowered
prepared-observer CPU from 22.18 to 14.50 ms and the frame median from
104.31 to 94.88 ms. All 25 measured native frames promoted. The extended
scripted route exited normally and promoted at 5040, 5100, 5200 and 5290;
a late capture retained the race HUD. A mode-boundary repeat passed. Its first
attempt entered a different car/camera state and correctly fell back with
`missing remainder car draw`, so that attempt cannot prove native mode
coverage. Both hot-toggle runs passed after the verifier checked their valid
`race-moving` HUD checkpoint rather than the pre-race cinematic at 4080.
Artifacts are under `.local/ray-manager-hash-profile-20260927/`,
`.local/ray-manager-hash-live-20260927/`,
`.local/ray-manager-hash-mode-repeat-20260927/` and
`.local/ray-manager-hash-toggle-repeat-20260927/`.

At about 95 ms per frame, responsiveness remains open. Prepared snapshot
work still takes about 11 ms, prepared observation about 15 ms, and native
output about 30 ms median in the measured window. Keep the whole-frame
fallback and repeat presentation checks after each change. A longer
unscripted drive remains open; scripted movement and static screenshots do
not prove control responsiveness.
