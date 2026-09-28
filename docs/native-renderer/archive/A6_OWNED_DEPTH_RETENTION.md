# A6 owned depth-clear retention — 2026-09-09

Historical: describes the Xenos-era renderer's owned depth clear, removed with the Xenos renderer in `6b75238`; kept for reference.

Status: **A6 complete; A epic closed for the retained 1x chain.**

## Retained scope

Retain the complete owned depth-clear chain at **symmetric 1x draw resolution**.
`fh1_owned_depth_clear` defaults to true, with an explicit 1x admission guard.
At 2x, asymmetric scales and other unsupported states, the established compatibility
path supplies the clear and its transfers. The 2x optimization is rejected for
retention; passing content checks did not outweigh its frame-time regression.

This is a bounded first-chain milestone, not complete Xenos retirement or a claim
that every rendering path is native. Single-sample ownership and draw-resolution
scale are different: the retained path maps a guest 4-sample D24S8 clear directly
onto its authoritative single-sample owner at 1x resolution.

## Clean performance evidence

Values below are means of per-run statistics, baseline â†’ candidate. Tests use
matched scripted scenes, exclude captures from timing windows and reject compiler
contention. Traffic/crowd workload varies between runs; these are not deterministic
replays or general hardware-requirement claims.

| Scene / accepted scope | Median ms | p95 ms | p99 ms | GPU ms | CPU seconds / wall second | Private MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1x ordinary, 8 longer ABBA/BAAB runs | 15.977 â†’ 15.715 | 19.336 â†’ 18.736 | 22.020 â†’ 20.967 | 15.259 â†’ 14.934 | 3.190 â†’ 3.162 | 2574.8 â†’ 2553.8 |
| 1x North Carson, 4 ABBA runs | 23.680 â†’ 19.279 | 33.884 â†’ 29.898 | 46.223 â†’ 46.030 | 21.476 â†’ 18.898 | 2.879 â†’ 2.957 | 2743.0 â†’ 2750.6 |
| **Rejected 2x** North Carson, 4 longer ABBA runs | 21.121 â†’ 21.462 | 29.953 â†’ 62.238 | 49.152 â†’ 93.433 | 21.546 â†’ 23.460 | 3.413 â†’ 3.332 | 4990.2 â†’ 4954.1 |

The 1x ordinary result improves median/p95/p99 by 1.64%/3.10%/4.78% in aggregate;
both longer blocks improve those statistics. The earlier short 1x batch was mixed
and remains recorded. North Carson's 1x median/p95 improve 18.58%/11.76%, p99 is
approximately flat, with a 2.74% CPU increase and 7.6 MiB private-memory increase.
These small resource increases accompany the demonstrated GPU/frame-time savings.

The initial 2x North Carson comparison worsened p99 24.46%. A predefined longer
86-second stationary window (source frame clock 62..148 seconds, ABBA order)
worsened p99 90.09% and p95 107.79%. All four arrival/motion/session checks passed,
with no GPU errors or competing compiler workload. Earlier ordinary-scene 2x gains
therefore do **not** qualify enabling this optimization at 2x.

A temporary same-binary off/on timing pair reproduced broad draw-processing wall-time
stalls in both modes. It did not establish the root cause. Profiling was removed;
no instrumented timings are used as performance-retention evidence. Further 2x
work needs a changed design or actual attribution, not unchanged repeated tests.

## Correctness, identity and reproduction

The [chain contract](OWNED_DEPTH_CHAIN_CONTRACT.md) records exact depth/stencil,
partial-region, mixed-history, EDRAM dump and final resolve-output checks, query
exclusions, cache eviction and same-key recreation coverage. The 1x retention
change adds a scale guard and changes the default; the qualified 1x clear/mapping,
resource lookup and publication implementation is unchanged.

- Qualified baseline DLL: `87B79AABEC3F06326DF616DC02CCC3588E1CFFEFEE3A4E2B3590A17655EA23B5`.
- Measured opt-in candidate DLL: `2B9CED2AEB9270BCECA5FC981BE8539D72A3101AC0B8F42CFF89AE3119F60F10`.
- Retained 1x-only default DLL: `27B486FD5BBD928B90186AF2778D8489F364B3C78993FDB7818CC8514E318B50`.
- Actual EXE: `882D9247F23B97B0121A0D7F15F3EFB4569728777A26F03EF2B676D8A9DBCB52`.

The runtime build manifest reports a stale different EXE hash; direct file hashes
are authoritative for these final tests. Retained candidate archive:
`.local/native-renderer/p2/owned-clear-retained-1x.dll`. Build command:
`cmake --build out/build/win-amd64-release --config Release --target rexgpu-fh1`
after entering the environment from `tools/release-common.ps1`.

Evidence under `.local/native-renderer/p2/`:

- `final-owned-1x-long-abba-summary.json`: ordinary repeated results.
- `north-carson-1x-v2-abba-summary.json`: retained-scope difficult-area comparison.
- `north-carson-2x-long-abba-summary.json` and `north-carson-long-session-audit.json`: rejected scaled comparison and session checks.
- `north-carson-2x-tail-v3-{a1,b1}/`: temporary attribution fixture only.
- `retained-owned-race-{1x,2x}-v2-probe/owned-qualification.json`: final default behavior; native counter required at 1x, zero owned clears required at 2x, plus captures, race HUD, presentation and simulation timing.
- `retained-owned-race.ps1 -Scale 1|2` and `check-retained-owned-race.py 1|2`: final local reproduction harness/checker using the repository race route and authorized AppData save.

Remaining compatibility includes guest packet generation/decoding, translated
pipeline validation, conflicting ownership transfers, independent floating-depth
history, EDRAM dump, resolve publication and synchronization. Device-loss recovery,
other scenes, long NPC/UI animation review and lower-end hardware remain outside
this bounded qualification. The 2x owned-clear optimization belongs in the next
resource-migration backlog; its rejection is not a claim that the broader town
slowdown has been fixed.

## Final validation

Release DLL build and production ownership/lifetime checker passed. The exact
retained DLL passed default-setting race-start checks at both scales:

| Scale | Session | Owned clear counter | Simulation / wall time | Result |
| --- | --- | ---: | ---: | --- |
| 1x | `20260909T222559Z-p18136` | 10,240 | 1.004550 | Native chain admitted; all scoped checks pass |
| 2x | `20260909T222721Z-p21300` | 0 | 1.003516 | Compatibility fallback; all scoped checks pass |

Both runs complete normally with all six captures, race HUD, visible vehicle
motion, presentation checks and no reported renderer errors. Race-moving images
were inspected at both scales. These are race-start smoke checks, not full-race
performance claims. The final scale/default-only change reuses the measured 1x
implementation; it does not make the rejected 2x optimization qualified.

The first final harness run accidentally selected baseline87 and correctly failed
its native-counter check. It is retained as invalid candidate evidence in
`retained-owned-race-1x-probe`; corrected v2 checks explicitly assert DLL identity.
Temporary profiling code is absent: the common command processor matches its
pre-instrumentation backup byte-for-byte. Staged and build-artifact DLLs use the
retained hash above. No save files were copied/reset/edited by tooling, and no
commit, remote push or release is part of A6 closure.
