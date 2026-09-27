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
usable race. Next isolate the 41 ms native output callback from the roughly
36 ms prepared snapshot/observer work in the live path, then remove one
measured duplicate cost at a time. Keep the whole-frame fallback and repeat
the native presentation checks after each change. A longer unscripted drive
remains open; scripted movement and static screenshots do not prove control
responsiveness.
