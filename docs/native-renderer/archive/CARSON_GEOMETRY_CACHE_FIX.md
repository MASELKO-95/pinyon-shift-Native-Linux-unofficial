# Carson geometry cache follow-up (2026-09-10)

Historical: describes the Xenos-era renderer's owned geometry cache, removed in `bccf126`; kept for reference.

The reported Carson race slowdown reproduces in **Hot Hatch Hustle**, reached
from the current AppData save. Native reflection mipmaps disabled still produce
about 100 ms median frame time. The slowdown is not explained by the new mip path.

## Cause and fix

The 32 MiB geometry cache repeatedly evicts and recreates geometry used by the
same scene. Periodic allocation counters rise from 3,478 to 14,978 in 9.3 seconds.
Repeated sampling of the GPU command thread finds allocation, GPU virtual-address
release and synchronization calls dominating its sampled PCs.

Keep geometry used in the current or previous frame resident. When the cache
cannot admit a new owner, use the existing shared-memory geometry path. Older
completed owners remain evictable. The 32 MiB/512-entry limit, memory watches,
import validation and in-flight protection remain in place. Buffer recycling and
contained-window experiments remain disabled.

`tools/check-fh1-geometry-cache.py` checks the production implementation, including
current/previous-frame rejection without new allocations and later eviction.
The Release renderer build and geometry checks pass.

## Runtime evidence

Local evidence is under `.local/native-renderer/b2/`. Race measurements use route
seconds 70 through 75.7 with verified CSV clock bounds, excluding screenshot
readbacks. Median and p95 are frame times; these short runs are not a sustained
benchmark or a promise for other hardware.

| Run | Scale | Native mips | Median ms | p95 ms | Mean draws |
| --- | --- | --- | ---: | ---: | ---: |
| `carson-race-repro-off-v1` | 2x | off | 100.499 | 134.727 | 7,418.6 |
| `carson-race-grace-v1` | 2x | off | 33.494 | 49.991 | 7,428.2 |
| `carson-race-grace-mips-1x-v1` | 1x | on | 30.439 | 37.994 | 7,383.1 |
| `carson-race-grace-mips-2x-v1` | 2x | on | 32.281 | 46.672 | 7,375.1 |

The matched 2x comparison improves from roughly 10 to 30 FPS with nearly equal
draw counts. Allocation churn falls substantially. Actual race screenshots show
intact geometry and HUD, and sessions exit normally.
Both original and fixed runs log the same four queued shader-pack misses during
the race introduction; these are not newly introduced by this fix. The checks
do not establish complete shader coverage or visual acceptance.

Carson free driving with the fix and mipmaps enabled measures 19.857 ms median,
24.487 ms p95 in the short town window. The car and traffic differ from the
original town run, so this does not establish a controlled town improvement or
close the user's broader Carson performance report. Test routes naturally
selected the owned Corrado; no save files were copied or restored.

The qualified renderer DLL SHA256 is
`006441640FF95AF3C99124F0101A8737DD60551477D16147DE73B3C639152510`.
Other runtime binaries come from `reflection-mip-retained-runtime-v1`; unrelated
SDK runtime edits are excluded from the tested runtime. Embedded executable
revision metadata is stale: use the archived binary identities for these runs.

## Still open

- Green flashes on the player's rear glass have not been reproduced. A clean
  Carson RenderDoc frame passes native mip generation/publication checks across
  8,847,360 bytes and 48 dispatches, but it is not a failing rear-glass frame.
  Capture the affected car and flash before changing reflection behavior.
- Retest Carson town and the race manually, including longer driving and traffic.
  Additional bottlenecks may remain despite removing this allocation churn.
- A tested float-constant upload batching candidate did not improve the race and
  was reverted. Its evidence is preserved locally, not included in this fix.

The broader resource migration and Xenos retirement backlog remains deferred.
