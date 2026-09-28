# Native renderer performance baselines

Xenos baselines and `native` results per route at 1x, for the Xenos
retirement backlog (XR-00, XR-07). Runs come from seed `appdata-2026-09-27`
through `tools/run-fh1-render-test.py --configuration RelWithDebInfo --hidden
--seed-pipeline-prewarm` with the shareable FH1 shader pack, on the
development machine (NVIDIA, vendor 10DE). Frame and GPU times come from the
per-frame performance CSV, skipping the first 120 rows. A regression is a
native median or p95 more than 3% above Xenos.

Routes must reach the same content on both renderers, so they synchronize on
game state (`wait` steps) instead of wall time: `fh1-race-sync` waits for the
car, the car-select thumbnails and the race's replay stream;
`fh1-modes-sync` waits for the car before driving, pausing, opening the map
and entering photo mode. `fh1-fmv` plays the opening movies. The older
wall-clock routes (`fh1-race`, `fh1-free-roam`, `fh1-map`, `fh1-pause`,
`fh1-photo-mode`) drift: the native and Xenos runs of `fh1-race` reached
different events, so they are not used for parity.

## Whole routes

| Route, renderer | Frames | Median ms | p95 ms | p99 ms | GPU median ms | GPU p95 ms | Draws/frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| fh1-fmv xenos | 1163 | 4.01 | 6.00 | 6.52 | 0.70 | 1.64 | 53 |
| fh1-fmv native | 1137 | 4.06 | 6.01 | 6.53 | 0.74 | 1.64 | 54 |
| fh1-fmv xenos (repeat) | 1132 | 4.10 | 6.01 | 6.52 | 0.64 | 1.63 | 53 |
| fh1-fmv native (repeat) | 1104 | 4.15 | 6.01 | 12.11 | 0.62 | 1.60 | 54 |
| fh1-race-sync xenos | 4749 | 13.78 | 28.08 | 33.60 | 5.54 | 11.94 | 2469 |
| fh1-race-sync native | 4751 | 13.58 | 27.49 | 33.52 | 5.89 | 13.04 | 2506 |
| fh1-modes-sync xenos | 4231 | 16.52 | 21.89 | 28.34 | 7.41 | 8.45 | 3781 |
| fh1-modes-sync native | 4217 | 15.59 | 21.55 | 29.14 | 8.12 | 9.71 | 2815 |

## Race window

The last 600 frames of `fh1-race-sync` are the moving race (about 5,800
draws per frame on both):

| Renderer | Median ms | p95 ms | GPU median ms | GPU p95 ms |
| --- | --- | --- | --- | --- |
| xenos | 25.19 | 30.23 | 11.60 | 12.04 |
| native | 24.95 | 30.15 | 12.61 | 13.00 |

Frame time is at parity (-1.0% median, -0.3% p95); race frames are
CPU-bound, and native GPU time is about 1 ms higher, mostly EDRAM ownership
transfers (depth ping-pongs between MSAA modes at one base that the scene
really needs, and color-to-depth transfers). Run-to-run p95 varies by up
to 8% on this route, so compare pairs run back to back.

Earlier builds for reference: before repeated-target preparation was
skipped, native prepared targets for 3.1 ms per race frame and the race
window was +1.8% median; before per-tile stencil state, the whole route was
+3.3-3.7% median.

## Memory

`FH1 native executor memory MB` (logged with the executor stats): textures
stay under the texture cache's soft limit of 384 MB (race 265, photo 178,
free roam 149 MB peaks); executor surfaces take 522-562 MB and the transfer
word buffer 10 MB.

## Not covered

Only this machine was measured; AMD, Intel and lower-end hardware are not
available (XR-09). Native mode is 1x only.
